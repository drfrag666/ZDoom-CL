/*
** c_dispatch.cpp
** Functions for executing console commands and aliases
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "templates.h"
#include "doomtype.h"
#include "cmdlib.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "m_argv.h"
#include "doomstat.h"
#include "m_alloc.h"
#include "d_player.h"
#include "configfile.h"
#include "m_crc32.h"
#include "v_text.h"

bool ParsingKeyConf;

static const char *KeyConfCommands[] =
{
	"alias",
	"defaultbind",
	"addkeysection",
	"addmenukey",
	"addslotdefault",
	"weaponsection",
	"setslot",
	"addplayerclass",
	"clearplayerclasses"
};

static long ParseCommandLine (const char *args, int *argc, char **argv);

CVAR (Bool, lookspring, true, CVAR_ARCHIVE);	// Generate centerview when -mlook encountered?

class DWaitingCommand : public DThinker
{
	DECLARE_CLASS (DWaitingCommand, DThinker)
public:
	DWaitingCommand (const char *cmd, int tics);
	~DWaitingCommand ();
	void Serialize (FArchive &arc);
	void Tick ();

private:
	DWaitingCommand ();

	char *Command;
	int TicsLeft;
};

class DStoredCommand : public DThinker
{
	DECLARE_CLASS (DStoredCommand, DThinker)
public:
	DStoredCommand (FConsoleCommand *com, const char *cmd);
	~DStoredCommand ();
	void Tick ();

private:
	DStoredCommand ();

	FConsoleCommand *Command;
	char *Text;
};

struct FActionMap
{
	unsigned int	Key;	// value from passing Name to MakeKey()
	FButtonStatus	*Button;
	char			Name[12];
};

static FConsoleCommand *FindNameInHashTable (FConsoleCommand **table, const char *name, size_t namelen);
static FConsoleCommand *ScanChainForName (FConsoleCommand *start, const char *name, size_t namelen, FConsoleCommand **prev);

FConsoleCommand *Commands[FConsoleCommand::HASH_SIZE];

FButtonStatus Button_Mlook, Button_Klook, Button_Use, Button_AltAttack,
	Button_Attack, Button_Speed, Button_MoveRight, Button_MoveLeft,
	Button_Strafe, Button_LookDown, Button_LookUp, Button_Back,
	Button_Forward, Button_Right, Button_Left, Button_MoveDown,
	Button_MoveUp, Button_Jump, Button_ShowScores, Button_Crouch;

// To add new actions, go to the console and type "key <action name>".
// This will give you the key value to use in the first column. Then
// insert your new action into this list so that the keys remain sorted
// in ascending order. No two keys can be identical. If yours matches
// an existing key, either modify MakeKey(), or (preferably) change the
// name of your action.

FActionMap ActionMaps[] =
{
	{ 0x0f26fef6, &Button_Speed,		"speed" },
	{ 0x1ccf57bf, &Button_MoveUp,		"moveup" },
	{ 0x22beba5f, &Button_Klook,		"klook" },
	{ 0x47c02d3b, &Button_Attack,		"attack" },
	{ 0x6dcec137, &Button_Back,			"back" },
	{ 0x7a67e768, &Button_Left,			"left" },
	{ 0x8076f318, &Button_Crouch,		"crouch" },
	{ 0x84b8789a, &Button_MoveLeft,		"moveleft" },
	{ 0x8fd9bf1e, &Button_ShowScores,	"showscores" },
	{ 0x94b1cc4b, &Button_Use,			"use" },
	{ 0xa7b30616, &Button_Jump,			"jump" },
	{ 0xadfe4fff, &Button_Mlook,		"mlook" },
	{ 0xb4ca7514, &Button_Right,		"right" },
	{ 0xb563e265, &Button_LookDown,		"lookdown" },
	{ 0xb67a0835, &Button_Strafe,		"strafe" },
	{ 0xc4704c1d, &Button_AltAttack,	"altattack" },
	{ 0xe2200fc9, &Button_MoveDown,		"movedown" },
	{ 0xe78739bb, &Button_MoveRight,	"moveright" },
	{ 0xe7912f86, &Button_Forward,		"forward" },
	{ 0xf01cb105, &Button_LookUp,		"lookup" },
};

#define NUM_ACTIONS countof(ActionMaps)



IMPLEMENT_CLASS (DWaitingCommand)

void DWaitingCommand::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << Command << TicsLeft;
}

DWaitingCommand::DWaitingCommand ()
{
	Command = NULL;
	TicsLeft = 1;
}

DWaitingCommand::DWaitingCommand (const char *cmd, int tics)
{
	Command = copystring (cmd);
	TicsLeft = tics+1;
}

DWaitingCommand::~DWaitingCommand ()
{
	if (Command != NULL)
	{
		delete[] Command;
	}
}

void DWaitingCommand::Tick ()
{
	if (--TicsLeft == 0)
	{
		AddCommandString (Command);
		Destroy ();
	}
}

IMPLEMENT_CLASS (DStoredCommand)

DStoredCommand::DStoredCommand ()
{
	Text = NULL;
	Destroy ();
}

DStoredCommand::DStoredCommand (FConsoleCommand *command, const char *args)
{
	Command = command;
	Text = copystring (args);
}		

DStoredCommand::~DStoredCommand ()
{
	if (Text != NULL)
	{
		delete[] Text;
	}
}

void DStoredCommand::Tick ()
{
	if (Text != NULL && Command != NULL)
	{
		FCommandLine args (Text);
		Command->Run (args, players[consoleplayer].mo, 0);
	}
	Destroy ();
}

static int ListActionCommands (const char *pattern)
{
	char matcher[16];
	unsigned int i;
	int count = 0;

	for (i = 0; i < NUM_ACTIONS; ++i)
	{
		if (pattern == NULL || CheckWildcards (pattern,
			(sprintf (matcher, "+%s", ActionMaps[i].Name), matcher)))
		{
			Printf ("+%s\n", ActionMaps[i].Name);
			count++;
		}
		if (pattern == NULL || CheckWildcards (pattern,
			(sprintf (matcher, "-%s", ActionMaps[i].Name), matcher)))
		{
			Printf ("-%s\n", ActionMaps[i].Name);
			count++;
		}
	}
	return count;
}

unsigned int MakeKey (const char *s)
{
	if (s == NULL)
	{
		return 0xffffffff;
	}

	DWORD key = 0xffffffff;
	const DWORD *table = GetCRCTable ();

	while (*s)
	{
		key = CRC1 (key, tolower (*s++), table);
	}
	return key ^ 0xffffffff;
}

unsigned int MakeKey (const char *s, size_t len)
{
	if (len == 0 || s == NULL)
	{
		return 0xffffffff;
	}

	DWORD key = 0xffffffff;
	const DWORD *table = GetCRCTable ();

	while (len > 0)
	{
		key = CRC1 (key, tolower(*s++), table);
		--len;
	}
	return key ^ 0xffffffff;
}

// FindButton scans through the actionbits[] array
// for a matching key and returns an index or -1 if
// the key could not be found. This uses binary search,
// so actionbits[] must be sorted in ascending order.

FButtonStatus *FindButton (unsigned int key)
{
	const FActionMap *bit;

	bit = BinarySearch<FActionMap, unsigned int>
			(ActionMaps, NUM_ACTIONS, &FActionMap::Key, key);
	return bit ? bit->Button : NULL;
}

void FButtonStatus::PressKey (int keynum)
{
	int i, open;

	keynum &= KEY_DBLCLICKED-1;

	if (keynum == 0)
	{ // Issued from console instead of a key, so force on
		Keys[0] = 0xffff;
		for (i = MAX_KEYS-1; i > 0; --i)
		{
			Keys[i] = 0;
		}
	}
	else
	{
		for (i = MAX_KEYS-1, open = -1; i >= 0; --i)
		{
			if (Keys[i] == 0)
			{
				open = i;
			}
			else if (Keys[i] == keynum)
			{ // Key is already down; do nothing
				return;
			}
		}
		if (open < 0)
		{ // No free key slots, so do nothing
			Printf ("More than %u keys pressed for a single action!\n", MAX_KEYS);
			return;
		}
		Keys[open] = keynum;
	}
	bDown = bWentDown = true;
}

void FButtonStatus::ReleaseKey (int keynum)
{
	int i, numdown, match;

	keynum &= KEY_DBLCLICKED-1;

	if (keynum == 0)
	{ // Issued from console instead of a key, so force off
		for (i = MAX_KEYS-1; i >= 0; --i)
		{
			Keys[i] = 0;
		}
		bWentUp = true;
		bDown = false;
	}
	else
	{
		for (i = MAX_KEYS-1, numdown = 0, match = -1; i >= 0; --i)
		{
			if (Keys[i] != 0)
			{
				++numdown;
				if (Keys[i] == keynum)
				{
					match = i;
				}
			}
		}
		if (match < 0)
		{ // Key was not down; do nothing
			return;
		}
		Keys[match] = 0;
		bWentUp = true;
		if (--numdown == 0)
		{
			bDown = false;
		}
	}
}

void ResetButtonTriggers ()
{
	for (int i = NUM_ACTIONS-1; i >= 0; --i)
	{
		ActionMaps[i].Button->ResetTriggers ();
	}
}

void ResetButtonStates ()
{
	for (int i = NUM_ACTIONS-1; i >= 0; --i)
	{
		FButtonStatus *button = ActionMaps[i].Button;

		if (button != &Button_Mlook && button != &Button_Klook)
		{
			button->ReleaseKey (0);
		}
		button->ResetTriggers ();
	}
}

void C_DoCommand (const char *cmd, int keynum)
{
	FConsoleCommand *com;
	const char *end;
	const char *beg;

	// Skip any beginning whitespace
	while (*cmd && *cmd <= ' ')
		cmd++;

	// Find end of the command name
	if (*cmd == '\"')
	{
		for (end = beg = cmd+1; *end && *end != '\"'; ++end)
			;
	}
	else
	{
		beg = cmd;
		for (end = cmd+1; *end > ' '; ++end)
			;
	}

	const int len = end - beg;

	if (ParsingKeyConf)
	{
		int i;

		for (i = countof(KeyConfCommands)-1; i >= 0; --i)
		{
			if (strnicmp (beg, KeyConfCommands[i], len) == 0 &&
				KeyConfCommands[i][len] == 0)
			{
				break;
			}
		}
		if (i < 0)
		{
			Printf ("Invalid command for KEYCONF: %s\n", beg);
			return;
		}
	}

	// Check if this is an action
	if (*beg == '+' || *beg == '-')
	{
		FButtonStatus *button;

		button = FindButton (MakeKey (beg + 1, end - beg - 1));
		if (button != NULL)
		{
			if (*beg == '+')
			{
				button->PressKey (keynum);
			}
			else
			{
				button->ReleaseKey (keynum);
				if (button == &Button_Mlook && lookspring)
				{
					Net_WriteByte (DEM_CENTERVIEW);
				}
			}
			return;
		}
	}
	
	// Parse it as a normal command
	// Checking for matching commands follows this search order:
	//	1. Check the Commands[] hash table
	//	2. Check the CVars list

	if ( (com = FindNameInHashTable (Commands, beg, len)) )
	{
		if (gamestate != GS_STARTUP || ParsingKeyConf ||
			(len == 3 && strnicmp (beg, "set", 3) == 0) ||
			(len == 7 && strnicmp (beg, "logfile", 7) == 0) ||
			(len == 9 && strnicmp (beg, "unbindall", 9) == 0) ||
			(len == 4 && strnicmp (beg, "bind", 4) == 0) ||
			(len == 4 && strnicmp (beg, "exec", 4) == 0) ||
			(len ==10 && strnicmp (beg, "doublebind", 10) == 0) ||
			(len == 6 && strnicmp (beg, "pullin", 6) == 0)
			)
		{
			FCommandLine args (beg);
			com->Run (args, players[consoleplayer].mo, keynum);
		}
		else
		{
			new DStoredCommand (com, beg);
		}
	}
	else
	{ // Check for any console vars that match the command
		FBaseCVar *var = FindCVarSub (beg, len);

		if (var != NULL)
		{
			FCommandLine args (beg);

			if (args.argc() >= 2)
			{ // Set the variable
				var->CmdSet (args[1]);
			}
			else
			{ // Get the variable's value
				UCVarValue val = var->GetGenericRep (CVAR_String);
				Printf ("\"%s\" is \"%s\"\n", var->GetName(), val.String);
			}
		}
		else
		{ // We don't know how to handle this command
			char cmdname[64];
			int minlen = MIN (len, 63);

			memcpy (cmdname, beg, minlen);
			cmdname[len] = 0;
			Printf ("Unknown command \"%s\"\n", cmdname);
		}
	}
}

void AddCommandString (char *cmd, int keynum)
{
	char *brkpt;
	int more;

	if (cmd)
	{
		while (*cmd)
		{
			brkpt = cmd;
			while (*brkpt != ';' && *brkpt != '\0')
			{
				if (*brkpt == '\"')
				{
					brkpt++;
					while (*brkpt != '\0' && (*brkpt != '\"' || *(brkpt-1) == '\\'))
						brkpt++;
				}
				if (*brkpt != '\0')
					brkpt++;
			}
			if (*brkpt == ';')
			{
				*brkpt = '\0';
				more = 1;
			}
			else
			{
				more = 0;
			}
			// Intercept wait commands here. Note: wait must be lowercase
			while (*cmd && *cmd <= ' ')
				cmd++;
			if (*cmd)
			{
				if (!ParsingKeyConf &&
					cmd[0] == 'w' && cmd[1] == 'a' && cmd[2] == 'i' && cmd[3] == 't' &&
					(cmd[4] == 0 || cmd[4] == ' '))
				{
					int tics;

					if (cmd[4] == ' ')
					{
						tics = strtol (cmd + 5, NULL, 0);
					}
					else
					{
						tics = 1;
					}
					if (tics > 0)
					{
						if (more)
						{ // The remainder of the command will be executed later
						  // Note that deferred commands lose track of which key
						  // (if any) they were pressed from.
							*brkpt = ';';
							new DWaitingCommand (brkpt, tics+1);
						}
						return;
					}
				}
				else
				{
					C_DoCommand (cmd, keynum);
				}
			}
			if (more)
			{
				*brkpt = ';';
			}
			cmd = brkpt + more;
		}
	}
}

// ParseCommandLine
//
// Parse a command line (passed in args). If argc is non-NULL, it will
// be set to the number of arguments. If argv is non-NULL, it will be
// filled with pointers to each argument; argv[0] should be initialized
// to point to a buffer large enough to hold all the arguments. The
// return value is the necessary size of this buffer.
//
// Special processing: Inside quoted strings, \" becomes just "
// $<cvar> is replaced by the contents of <cvar>

static long ParseCommandLine (const char *args, int *argc, char **argv)
{
	int count;
	char *buffplace;

	count = 0;
	buffplace = NULL;
	if (argv != NULL)
	{
		buffplace = argv[0];
	}

	for (;;)
	{
		while (*args <= ' ' && *args)
		{ // skip white space
			args++;
		}
		if (*args == 0)
		{
			break;
		}
		else if (*args == '\"')
		{ // read quoted string
			char stuff;
			if (argv != NULL)
			{
				argv[count] = buffplace;
			}
			count++;
			args++;
			do
			{
				stuff = *args++;
				if (stuff == '\\' && *args == '\"')
				{
					stuff = '\"', args++;
				}
				else if (stuff == '\"')
				{
					stuff = 0;
				}
				else if (stuff == 0)
				{
					args--;
				}
				if (argv != NULL)
				{
					*buffplace = stuff;
				}
				buffplace++;
			} while (stuff);
		}
		else
		{ // read unquoted string
			const char *start = args++, *end;
			FBaseCVar *var;
			UCVarValue val;

			while (*args && *args > ' ' && *args != '\"')
				args++;
			if (*start == '$' && (var = FindCVarSub (start+1, args-start-1)))
			{
				val = var->GetGenericRep (CVAR_String);
				start = val.String;
				end = start + strlen (start);
			}
			else
			{
				end = args;
			}
			if (argv != NULL)
			{
				argv[count] = buffplace;
				while (start < end)
					*buffplace++ = *start++;
				*buffplace++ = 0;
			}
			else
			{
				buffplace += end - start + 1;
			}
			count++;
		}
	}
	if (argc != NULL)
	{
		*argc = count;
	}
	return (long)(buffplace - (char *)0);
}

FCommandLine::FCommandLine (const char *commandline)
{
	cmd = commandline;
	_argc = -1;
	_argv = NULL;
}

FCommandLine::~FCommandLine ()
{
	if (_argv != NULL)
	{
		delete[] _argv;
	}
}

int FCommandLine::argc ()
{
	if (_argc == -1)
	{
		argsize = ParseCommandLine (cmd, &_argc, NULL);
	}
	return _argc;
}

char *FCommandLine::operator[] (int i)
{
	if (_argv == NULL)
	{
		int count = argc();
		_argv = new char *[count + (argsize+sizeof(char*)-1)/sizeof(char*)];
		_argv[0] = (char *)_argv + count*sizeof(char *);
		ParseCommandLine (cmd, NULL, _argv);
	}
	return _argv[i];
}

static FConsoleCommand *ScanChainForName (FConsoleCommand *start, const char *name, size_t namelen, FConsoleCommand **prev)
{
	int comp;

	*prev = NULL;
	while (start)
	{
		comp = strnicmp (start->m_Name, name, namelen);
		if (comp > 0)
			return NULL;
		else if (comp == 0 && start->m_Name[namelen] == 0)
			return start;

		*prev = start;
		start = start->m_Next;
	}
	return NULL;
}

static FConsoleCommand *FindNameInHashTable (FConsoleCommand **table, const char *name, size_t namelen)
{
	FConsoleCommand *dummy;

	return ScanChainForName (table[MakeKey (name, namelen) % FConsoleCommand::HASH_SIZE], name, namelen, &dummy);
}

bool FConsoleCommand::AddToHash (FConsoleCommand **table)
{
	unsigned int key;
	FConsoleCommand *insert, **bucket;

	if (!stricmp (m_Name, "toggle"))
		key = 1;

	key = MakeKey (m_Name);
	bucket = &table[key % HASH_SIZE];

	if (ScanChainForName (*bucket, m_Name, strlen (m_Name), &insert))
	{
		return false;
	}
	else
	{
		if (insert)
		{
			m_Next = insert->m_Next;
			if (m_Next)
				m_Next->m_Prev = &m_Next;
			insert->m_Next = this;
			m_Prev = &insert->m_Next;
		}
		else
		{
			m_Next = *bucket;
			*bucket = this;
			m_Prev = bucket;
			if (m_Next)
				m_Next->m_Prev = &m_Next;
		}
	}
	return true;
}

FConsoleCommand::FConsoleCommand (const char *name, CCmdRun runFunc)
	: m_RunFunc (runFunc)
{
	static bool firstTime = true;

	if (firstTime)
	{
		char tname[16];
		unsigned int i;

		firstTime = false;

		// Add all the action commands for tab completion
		for (i = 0; i < NUM_ACTIONS; i++)
		{
			strcpy (&tname[1], ActionMaps[i].Name);
			tname[0] = '+';
			C_AddTabCommand (tname);
			tname[0] = '-';
			C_AddTabCommand (tname);
		}
	}

	int ag = strcmp (name, "kill");
	if (ag == 0)
		ag=0;
	m_Name = copystring (name);

	if (!AddToHash (Commands))
		Printf ("FConsoleCommand c'tor: %s exists\n", name);
	else
		C_AddTabCommand (name);
}

FConsoleCommand::~FConsoleCommand ()
{
	*m_Prev = m_Next;
	if (m_Next)
		m_Next->m_Prev = m_Prev;
	C_RemoveTabCommand (m_Name);
	delete[] m_Name;
}

void FConsoleCommand::Run (FCommandLine &argv, APlayerPawn *who, int key)
{
	m_RunFunc (argv, who, key);
}

FConsoleAlias::FConsoleAlias (const char *name, const char *command, bool noSave)
	: FConsoleCommand (name, NULL),
	  bRunning (false), bKill (false)
{
	m_Command[noSave] = copystring (command);
	m_Command[!noSave] = NULL;
}

FConsoleAlias::~FConsoleAlias ()
{
	for (int i = 0; i < 2; ++i)
	{
		if (m_Command[i] != NULL)
		{
			delete[] m_Command[i];
			m_Command[i] = NULL;
		}
	}
}

char *BuildString (int argc, char **argv)
{
	char temp[1024];
	char *cur;
	int arg;

	if (argc == 1)
	{
		return copystring (*argv);
	}
	else
	{
		cur = temp;
		for (arg = 0; arg < argc; arg++)
		{
			if (strchr (argv[arg], ' '))
			{
				cur += sprintf (cur, "\"%s\" ", argv[arg]);
			}
			else
			{
				cur += sprintf (cur, "%s ", argv[arg]);
			}
		}
		temp[strlen (temp) - 1] = 0;
		return copystring (temp);
	}
}

static int DumpHash (FConsoleCommand **table, BOOL aliases, const char *pattern=NULL)
{
	int bucket, count;
	FConsoleCommand *cmd;

	for (bucket = count = 0; bucket < FConsoleCommand::HASH_SIZE; bucket++)
	{
		cmd = table[bucket];
		while (cmd)
		{
			if (CheckWildcards (pattern, cmd->m_Name))
			{
				if (cmd->IsAlias())
				{
					if (aliases)
					{
						++count;
						static_cast<FConsoleAlias *>(cmd)->PrintAlias ();
					}
				}
				else if (!aliases)
				{
					++count;
					cmd->PrintCommand ();
				}
			}
			cmd = cmd->m_Next;
		}
	}
	return count;
}

void FConsoleAlias::PrintAlias ()
{
	if (m_Command[0])
	{
		Printf (TEXTCOLOR_YELLOW "%s : %s\n", m_Name, m_Command[0]);
	}
	if (m_Command[1])
	{
		Printf (TEXTCOLOR_ORANGE "%s : %s\n", m_Name, m_Command[1]);
	}
}

void FConsoleAlias::Archive (FConfigFile *f)
{
	if (f != NULL && m_Command[0] != NULL)
	{
		f->SetValueForKey ("Name", m_Name, true);
		f->SetValueForKey ("Command", m_Command[0], true);
	}
}

void C_ArchiveAliases (FConfigFile *f)
{
	int bucket;
	FConsoleCommand *alias;

	for (bucket = 0; bucket < FConsoleCommand::HASH_SIZE; bucket++)
	{
		alias = Commands[bucket];
		while (alias)
		{
			if (alias->IsAlias())
				static_cast<FConsoleAlias *>(alias)->Archive (f);
			alias = alias->m_Next;
		}
	}
}

// This is called only by the ini parser.
void C_SetAlias (const char *name, const char *cmd)
{
	FConsoleCommand *prev, *alias, **chain;

	chain = &Commands[MakeKey (name) % FConsoleCommand::HASH_SIZE];
	alias = ScanChainForName (*chain, name, strlen (name), &prev);
	if (alias != NULL)
	{
		if (!alias->IsAlias ())
		{
			//Printf (PRINT_BOLD, "%s is a command and cannot be an alias.\n", name);
			return;
		}
		delete alias;
	}
	new FConsoleAlias (name, cmd, false);
}

CCMD (alias)
{
	FConsoleCommand *prev, *alias, **chain;

	if (argv.argc() == 1)
	{
		Printf ("Current alias commands:\n");
		DumpHash (Commands, true);
	}
	else
	{
		chain = &Commands[MakeKey (argv[1]) % FConsoleCommand::HASH_SIZE];

		if (argv.argc() == 2)
		{ // Remove the alias

			if ( (alias = ScanChainForName (*chain, argv[1], strlen (argv[1]), &prev)))
			{
				if (alias->IsAlias ())
				{
					static_cast<FConsoleAlias *> (alias)->SafeDelete ();
				}
				else
				{
					Printf ("%s is a normal command\n", alias->m_Name);
				}
			}
		}
		else
		{ // Add/change the alias

			alias = ScanChainForName (*chain, argv[1], strlen (argv[1]), &prev);
			if (alias != NULL)
			{
				if (alias->IsAlias ())
				{
					static_cast<FConsoleAlias *> (alias)->Realias (argv[2], ParsingKeyConf);
				}
				else
				{
					Printf ("%s is a normal command\n", alias->m_Name);
					alias = NULL;
				}
			}
			else
			{
				alias = new FConsoleAlias (argv[1], argv[2], ParsingKeyConf);
			}
		}
	}
}

CCMD (cmdlist)
{
	int count;
	const char *filter = (argv.argc() == 1 ? NULL : argv[1]);

	count = ListActionCommands (filter);
	count += DumpHash (Commands, false, filter);
	Printf ("%d commands\n", count);
}

CCMD (key)
{
	if (argv.argc() > 1)
	{
		int i;

		for (i = 1; i < argv.argc(); ++i)
		{
			unsigned int key = MakeKey (argv[i]);
			Printf (" 0x%08x\n", key);
		}
	}
}

// Execute any console commands specified on the command line.
// These all begin with '+' as opposed to '-'.
void C_ExecCmdLineParams ()
{
	for (int currArg = 1; currArg < Args.NumArgs(); )
	{
		if (*Args.GetArg (currArg++) == '+')
		{
			char *cmdString;
			int cmdlen = 1;
			int argstart = currArg - 1;

			while (currArg < Args.NumArgs())
			{
				if (*Args.GetArg (currArg) == '-' || *Args.GetArg (currArg) == '+')
					break;
				currArg++;
				cmdlen++;
			}

			if ( (cmdString = BuildString (cmdlen, Args.GetArgList (argstart))) )
			{
				C_DoCommand (cmdString + 1);
				delete[] cmdString;
			}
		}
	}
}

bool FConsoleCommand::IsAlias ()
{
	return false;
}

bool FConsoleAlias::IsAlias ()
{
	return true;
}

void FConsoleAlias::Run (FCommandLine &args, APlayerPawn *who, int key)
{
	if (bRunning)
	{
		Printf ("Alias %s tried to recurse.\n", m_Name);
		return;
	}

	int index = m_Command[1] != NULL;
	char *mycommand = m_Command[index];
	m_Command[index] = NULL;
	bRunning = true;
	AddCommandString (mycommand, key);
	bRunning = false;
	if (m_Command[index] != NULL)
	{ // The alias realiased itself, so delete the memory used by this command.
		delete[] mycommand;
	}
	else
	{ // The alias is unchanged, so put the command back so it can be used again.
		m_Command[index] = mycommand;
	}
	if (bKill)
	{ // The alias wants to remove itself
		delete this;
	}
}

void FConsoleAlias::Realias (const char *command, bool noSave)
{
	if (m_Command[1] != NULL)
	{
		noSave = true;
	}
	if (m_Command[noSave] != NULL)
	{
		delete[] m_Command[noSave];
	}
	m_Command[noSave] = copystring (command);
	bKill = false;
}

void FConsoleAlias::SafeDelete ()
{
	if (!bRunning)
	{
		delete this;
	}
	else
	{
		bKill = true;
	}
}

extern void D_AddFile (const char *file);
static byte PullinBad = 2;
static const char *PullinFile;

int C_ExecFile (const char *file, bool usePullin)
{
	FILE *f;
	char cmd[4096];
	int retval = 0;

	BYTE pullinSaved = PullinBad;
	const char *fileSaved = PullinFile;

	if ( (f = fopen (file, "r")) )
	{
		PullinBad = 1-usePullin;
		PullinFile = file;

		while (fgets (cmd, 4095, f))
		{
			// Comments begin with //
			char *stop = cmd + strlen (cmd) - 1;
			char *comment = cmd;
			int inQuote = 0;

			if (*stop == '\n')
				*stop-- = 0;

			while (comment < stop)
			{
				if (*comment == '\"')
				{
					inQuote ^= 1;
				}
				else if (!inQuote && *comment == '/' && *(comment + 1) == '/')
				{
					break;
				}
				comment++;
			}
			if (comment == cmd)
			{ // Comment at line beginning
				continue;
			}
			else if (comment < stop)
			{ // Comment in middle of line
				*comment = 0;
			}

			AddCommandString (cmd);
		}
		if (!feof (f))
		{
			retval = 2;
		}
		fclose (f);
	}
	else
	{
		retval = 1;
	}
	PullinBad = pullinSaved;
	PullinFile = fileSaved;
	return retval;
}

CCMD (pullin)
{
	if (PullinBad == 2)
	{
		Printf ("This command is only valid from .cfg\n"
				"files and only when used at startup.\n");
	}
	else if (argv.argc() > 1)
	{
		const char *lastSlash;

#ifdef unix
		lastSlash = strrchr (PullinFile, '/');
#else
		const char *lastSlash1, *lastSlash2;

		lastSlash1 = strrchr (PullinFile, '/');
		lastSlash2 = strrchr (PullinFile, '\\');
		lastSlash = MAX (lastSlash1, lastSlash2);
#endif

		if (PullinBad)
		{
			Printf ("Not loading:");
		}
		for (int i = 1; i < argv.argc(); ++i)
		{
			if (PullinBad)
			{
				Printf (" %s", argv[i]);
			}
			else
			{
				// Try looking for the wad in the same directory as the .cfg
				// before looking for it in the current directory.
				char *path = argv[i];

				if (lastSlash != NULL)
				{
					size_t pathlen = lastSlash - PullinFile + strlen (argv[i]) + 2;
					path = new char[pathlen];
					strncpy (path, PullinFile, (lastSlash - PullinFile) + 1);
					strcpy (path + (lastSlash - PullinFile) + 1, argv[i]);
					if (!FileExists (path))
					{
						delete[] path;
						path = argv[i];
					}
					else
					{
						FixPathSeperator (path);
					}
				}
				D_AddFile (path);
				if (path != argv[i])
				{
					delete[] path;
				}
			}
		}
		if (PullinBad)
		{
			Printf ("\n");
		}
	}
}

