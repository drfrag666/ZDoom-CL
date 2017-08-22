// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:  none
//
//-----------------------------------------------------------------------------



#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#include "templates.h"
#include "version.h"
#include "m_alloc.h"
#include "doomdef.h" 
#include "doomstat.h"
#include "d_protocol.h"
#include "d_netinf.h"
#include "f_finale.h"
#include "m_argv.h"
#include "m_misc.h"
#include "m_menu.h"
#include "m_random.h"
#include "m_crc32.h"
#include "i_system.h"
#include "p_setup.h"
#include "p_saveg.h"
#include "p_effect.h"
#include "p_tick.h"
#include "d_main.h"
#include "wi_stuff.h"
#include "hu_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_bind.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "w_wad.h"
#include "p_local.h" 
#include "s_sound.h"
#include "gstrings.h"
#include "r_data.h"
#include "r_sky.h"
#include "r_draw.h"
#include "g_game.h"
#include "g_level.h"
#include "b_bot.h"			//Added by MC:
#include "sbar.h"
#include "m_swap.h"
#include "m_png.h"
#include "gi.h"
#include "a_keys.h"
#include "a_artifacts.h"

#include <zlib.h>

#include "g_hub.h"


static FRandom pr_dmspawn ("DMSpawn");

const int SAVEPICWIDTH = 216;
const int SAVEPICHEIGHT = 162;

BOOL	G_CheckDemoStatus (void);
void	G_ReadDemoTiccmd (ticcmd_t *cmd, int player);
void	G_WriteDemoTiccmd (ticcmd_t *cmd, int player, int buf);
void	G_PlayerReborn (int player);

void	G_DoNewGame (void);
void	G_DoLoadGame (void);
void	G_DoPlayDemo (void);
void	G_DoCompleted (void);
void	G_DoVictory (void);
void	G_DoWorldDone (void);
void	G_DoSaveGame (bool okForQuicksave);
void	G_DoAutoSave (void);

FIntCVar gameskill ("skill", 2, CVAR_SERVERINFO|CVAR_LATCH);
CVAR (Int, deathmatch, 0, CVAR_SERVERINFO|CVAR_LATCH);
CVAR (Bool, chasedemo, false, 0);
CVAR (Bool, storesavepic, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

gameaction_t	gameaction;
gamestate_t 	gamestate = GS_STARTUP;
int 			respawnmonsters;

int 			paused;
bool 			sendpause;				// send a pause event next tic 
bool			sendsave;				// send a save event next tic 
bool			sendturn180;			// [RH] send a 180 degree turn next tic
bool 			usergame;				// ok to save / end game
bool			insave;					// Game is saving - used to block exit commands

BOOL			timingdemo; 			// if true, exit with report on completion 
BOOL 			nodrawers;				// for comparative timing purposes 
BOOL 			noblit; 				// for comparative timing purposes 

bool	 		viewactive;

BOOL 			netgame;				// only true if packets are broadcast 
BOOL			multiplayer;
player_t		players[MAXPLAYERS];
bool			playeringame[MAXPLAYERS];

int 			consoleplayer;			// player taking events
int 			gametic;

CVAR(Bool, demo_compress, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
char			demoname[256];
bool 			demorecording;
bool 			demoplayback;
bool 			netdemo;
bool			demonew;				// [RH] Only used around G_InitNew for demos
int				demover;
byte*			demobuffer;
byte*			demo_p;
byte*			democompspot;
byte*			demobodyspot;
size_t			maxdemosize;
byte*			zdemformend;			// end of FORM ZDEM chunk
byte*			zdembodyend;			// end of ZDEM BODY chunk
BOOL 			singledemo; 			// quit after playing a demo from cmdline 
 
BOOL 			precache = true;		// if true, load all graphics at start 
 
wbstartstruct_t wminfo; 				// parms for world map / intermission 
 
short			consistancy[MAXPLAYERS][BACKUPTICS];
 
byte*			savebuffer;
 
 
#define MAXPLMOVE				(forwardmove[1]) 
 
#define TURBOTHRESHOLD	12800

float	 		normforwardmove[2] = {0x19, 0x32};		// [RH] For setting turbo from console
float	 		normsidemove[2] = {0x18, 0x28};			// [RH] Ditto

fixed_t			forwardmove[2], sidemove[2];
fixed_t 		angleturn[4] = {640, 1280, 320, 320};		// + slow turn
fixed_t			flyspeed[2] = {1*256, 3*256};
int				lookspeed[2] = {450, 512};

#define SLOWTURNTICS	6 

CVAR (Bool,		cl_run,			true,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)		// Always run?
CVAR (Bool,		invertmouse,	false,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)		// Invert mouse look down/up?
CVAR (Bool,		freelook,		true,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)		// Always mlook?
CVAR (Bool,		lookstrafe,		false,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)		// Always strafe with mouse?
CVAR (Float,	m_pitch,		1.f,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)		// Mouse speeds
CVAR (Float,	m_yaw,			1.f,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR (Float,	m_forward,		1.f,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR (Float,	m_side,			2.f,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
 
int 			turnheld;								// for accelerative turning 
 
// mouse values are used once 
int 			mousex;
int 			mousey; 		

FString			savegamefile;
char			savedescription[SAVESTRINGSIZE];

// [RH] Name of screenshot file to generate (usually NULL)
char			*shotfile;

AActor* 		bodyque[BODYQUESIZE]; 
int 			bodyqueslot; 

void R_ExecuteSetViewSize (void);

FString savename;
FString BackupSaveName;

bool SendLand;
const AInventory *SendItemUse, *SendItemDrop;

EXTERN_CVAR (Int, team)

CVAR (Bool, teamplay, false, CVAR_SERVERINFO)

// [RH] Allow turbo setting anytime during game
CUSTOM_CVAR (Float, turbo, 100.f, 0)
{
	if (self < 10.f)
	{
		self = 10.f;
	}
	else if (self > 256.f)
	{
		self = 256.f;
	}
	else
	{
		float scale = self * 0.01f;

		forwardmove[0] = (int)(normforwardmove[0]*scale);
		forwardmove[1] = (int)(normforwardmove[1]*scale);
		sidemove[0] = (int)(normsidemove[0]*scale);
		sidemove[1] = (int)(normsidemove[1]*scale);
	}
}

CCMD (turnspeeds)
{
	if (argv.argc() == 1)
	{
		Printf ("Current turn speeds: %ld %ld %ld %ld\n", angleturn[0],
			angleturn[1], angleturn[2], angleturn[3]);
	}
	else
	{
		int i;

		for (i = 1; i <= 4 && i < argv.argc(); ++i)
		{
			angleturn[i-1] = atoi (argv[i]);
		}
		if (i <= 2)
		{
			angleturn[1] = angleturn[0] * 2;
		}
		if (i <= 3)
		{
			angleturn[2] = angleturn[0] / 2;
		}
		if (i <= 4)
		{
			angleturn[3] = angleturn[2];
		}
	}
}

CCMD (slot)
{
	if (argv.argc() > 1)
	{
		int slot = atoi (argv[1]);

		if (slot < NUM_WEAPON_SLOTS)
		{
			SendItemUse = LocalWeapons.Slots[slot].PickWeapon (&players[consoleplayer]);
		}
	}
}

CCMD (centerview)
{
	Net_WriteByte (DEM_CENTERVIEW);
}

CCMD(crouch)
{
	Net_WriteByte(DEM_CROUCH);
}

CCMD (land)
{
	SendLand = true;
}

CCMD (pause)
{
	sendpause = true;
}

CCMD (turn180)
{
	sendturn180 = true;
}

CCMD (weapnext)
{
	SendItemUse = PickNextWeapon (&players[consoleplayer]);
}

CCMD (weapprev)
{
	SendItemUse = PickPrevWeapon (&players[consoleplayer]);
}

CCMD (invnext)
{
	AInventory *next;

	if (m_Instigator == NULL)
		return;

	if (m_Instigator->player->InvSel != NULL)
	{
		if ((next = m_Instigator->player->InvSel->NextInv()) != NULL)
		{
			m_Instigator->player->InvSel = next;
		}
		else
		{
			// Select the first item in the inventory
			if (!(m_Instigator->Inventory->ItemFlags & IF_INVBAR))
			{
				m_Instigator->player->InvSel = m_Instigator->Inventory->NextInv();
			}
			else
			{
				m_Instigator->player->InvSel = m_Instigator->Inventory;
			}
		}
	}
	m_Instigator->player->inventorytics = 5*TICRATE;
}

CCMD (invprev)
{
	AInventory *item, *newitem;

	if (m_Instigator == NULL)
		return;

	if (m_Instigator->player->InvSel != NULL)
	{
		if ((item = m_Instigator->player->InvSel->PrevInv()) != NULL)
		{
			m_Instigator->player->InvSel = item;
		}
		else
		{
			// Select the last item in the inventory
			item = m_Instigator->player->InvSel;
			while ((newitem = item->NextInv()) != NULL)
			{
				item = newitem;
			}
			m_Instigator->player->InvSel = item;
		}
	}
	m_Instigator->player->inventorytics = 5*TICRATE;
}

CCMD (invuseall)
{
	SendItemUse = (const AInventory *)1;
}

CCMD (invuse)
{
	if (players[consoleplayer].inventorytics == 0 || gameinfo.gametype == GAME_Strife)
	{
		SendItemUse = players[consoleplayer].InvSel;
	}
	players[consoleplayer].inventorytics = 0;
}

CCMD (use)
{
	if (argv.argc() > 1 && m_Instigator != NULL)
	{
		SendItemUse = m_Instigator->FindInventory (PClass::FindClass (argv[1]));
	}
}

CCMD (invdrop)
{
	SendItemDrop = players[consoleplayer].InvSel;
}

CCMD (drop)
{
	if (argv.argc() > 1 && m_Instigator != NULL)
	{
		SendItemDrop = m_Instigator->FindInventory (PClass::FindClass (argv[1]));
	}
}

CCMD (useflechette)
{ // Select from one of arti_poisonbag1-3, whichever the player has
	static const ENamedName bagnames[3] =
	{
		NAME_ArtiPoisonBag1,
		NAME_ArtiPoisonBag2,
		NAME_ArtiPoisonBag3
	};
	int i, j;

	if (m_Instigator == NULL)
		return;

	if (m_Instigator->IsKindOf (PClass::FindClass (NAME_ClericPlayer)))
		i = 0;
	else if (m_Instigator->IsKindOf (PClass::FindClass (NAME_MagePlayer)))
		i = 1;
	else
		i = 2;

	for (j = 0; j < 3; ++j)
	{
		const PClass *type = PClass::FindClass (bagnames[(i+j)%3]);
		AInventory *item;
		if (type != NULL && (item = m_Instigator->FindInventory (type)))
		{
			SendItemUse = item;
			break;
		}
	}
}

CCMD (select)
{
	if (argv.argc() > 1)
	{
		AInventory *item = m_Instigator->FindInventory (PClass::FindClass (argv[1]));
		if (item != NULL)
		{
			m_Instigator->player->InvSel = item;
		}
	}
	m_Instigator->player->inventorytics = 5*TICRATE;
}

//
// G_BuildTiccmd
// Builds a ticcmd from all of the available inputs
// or reads it from the demo buffer.
// If recording a demo, write it out
//
void G_BuildTiccmd (ticcmd_t *cmd)
{
	int 		strafe;
	int 		speed;
	int 		forward;
	int 		side;
	int			fly;

	ticcmd_t	*base;

	base = I_BaseTiccmd (); 			// empty, or external driver
	*cmd = *base;

	cmd->consistancy = consistancy[consoleplayer][(maketic/ticdup)%BACKUPTICS];

	strafe = Button_Strafe.bDown;
	speed = Button_Speed.bDown ^ (int)cl_run;

	forward = side = fly = 0;

	// [RH] only use two stage accelerative turning on the keyboard
	//		and not the joystick, since we treat the joystick as
	//		the analog device it is.
	if (Button_Left.bDown || Button_Right.bDown)
		turnheld += ticdup;
	else
		turnheld = 0;

	// let movement keys cancel each other out
	if (strafe)
	{
		if (Button_Right.bDown)
			side += sidemove[speed];
		if (Button_Left.bDown)
			side -= sidemove[speed];
	}
	else
	{
		int tspeed = speed;

		if (turnheld < SLOWTURNTICS)
			tspeed *= 2;		// slow turn
		
		if (Button_Right.bDown)
		{
			G_AddViewAngle (angleturn[tspeed]);
			LocalKeyboardTurner = true;
		}
		if (Button_Left.bDown)
		{
			G_AddViewAngle (-angleturn[tspeed]);
			LocalKeyboardTurner = true;
		}
	}

	if (Button_LookUp.bDown)
		G_AddViewPitch (lookspeed[speed]);
	if (Button_LookDown.bDown)
		G_AddViewPitch (-lookspeed[speed]);

	if (Button_MoveUp.bDown)
		fly += flyspeed[speed];
	if (Button_MoveDown.bDown)
		fly -= flyspeed[speed];

	if (Button_Klook.bDown)
	{
		if (Button_Forward.bDown)
			G_AddViewPitch (lookspeed[speed]);
		if (Button_Back.bDown)
			G_AddViewPitch (-lookspeed[speed]);
	}
	else
	{
		if (Button_Forward.bDown)
			forward += forwardmove[speed];
		if (Button_Back.bDown)
			forward -= forwardmove[speed];
	}

	if (Button_MoveRight.bDown)
		side += sidemove[speed];
	if (Button_MoveLeft.bDown)
		side -= sidemove[speed];

	// buttons
	if (Button_Attack.bDown)
		cmd->ucmd.buttons |= BT_ATTACK;

	if (Button_AltAttack.bDown)
		cmd->ucmd.buttons |= BT_ALTATTACK;

	if (Button_Use.bDown)
		cmd->ucmd.buttons |= BT_USE;

	if (Button_Jump.bDown)
		cmd->ucmd.buttons |= BT_JUMP;

	if (Button_Crouch.bDown)
		cmd->ucmd.buttons |= BT_DUCK;

	// [RH] Scale joystick moves to full range of allowed speeds
	if (JoyAxes[JOYAXIS_PITCH] != 0)
	{
		G_AddViewPitch (int((JoyAxes[JOYAXIS_PITCH] * 2048) / 256));
		LocalKeyboardTurner = true;
	}
	if (JoyAxes[JOYAXIS_YAW] != 0)
	{
		G_AddViewAngle (int((-1280 * JoyAxes[JOYAXIS_YAW]) / 256));
		LocalKeyboardTurner = true;
	}

	side += int((MAXPLMOVE * JoyAxes[JOYAXIS_SIDE]) / 256);
	forward += int((JoyAxes[JOYAXIS_FORWARD] * MAXPLMOVE) / 256);
	fly += int(JoyAxes[JOYAXIS_UP] * 8);

	if (!Button_Mlook.bDown && !freelook)
	{
		forward += (int)((float)mousey * m_forward);
	}

	cmd->ucmd.pitch = LocalViewPitch >> 16;

	if (SendLand)
	{
		SendLand = false;
		fly = -32768;
	}

	if (strafe || lookstrafe)
		side += (int)((float)mousex * m_side);

	mousex = mousey = 0;

	if (forward > MAXPLMOVE)
		forward = MAXPLMOVE;
	else if (forward < -MAXPLMOVE)
		forward = -MAXPLMOVE;
	if (side > MAXPLMOVE)
		side = MAXPLMOVE;
	else if (side < -MAXPLMOVE)
		side = -MAXPLMOVE;

	cmd->ucmd.forwardmove += forward;
	cmd->ucmd.sidemove += side;
	cmd->ucmd.yaw = LocalViewAngle >> 16;
	cmd->ucmd.upmove = fly;
	LocalViewAngle = 0;
	LocalViewPitch = 0;

	// special buttons
	if (sendturn180)
	{
		sendturn180 = false;
		cmd->ucmd.buttons |= BT_TURN180;
	}
	if (sendpause)
	{
		sendpause = false;
		Net_WriteByte (DEM_PAUSE);
	}
	if (sendsave)
	{
		sendsave = false;
		Net_WriteByte (DEM_SAVEGAME);
		Net_WriteString (savegamefile);
		Net_WriteString (savedescription);
		savegamefile = "";
	}
	if (SendItemUse == (const AInventory *)1)
	{
		Net_WriteByte (DEM_INVUSEALL);
		SendItemUse = NULL;
	}
	else if (SendItemUse != NULL)
	{
		Net_WriteByte (DEM_INVUSE);
		Net_WriteLong (SendItemUse->InventoryID);
		SendItemUse = NULL;
	}
	if (SendItemDrop != NULL)
	{
		Net_WriteByte (DEM_INVDROP);
		Net_WriteLong (SendItemDrop->InventoryID);
		SendItemDrop = NULL;
	}

	cmd->ucmd.forwardmove <<= 8;
	cmd->ucmd.sidemove <<= 8;
}

//[Graf Zahl] This really helps if the mouse update rate can't be increased!
CVAR (Bool,		smooth_mouse,	false,	CVAR_GLOBALCONFIG|CVAR_ARCHIVE)

void G_AddViewPitch (int look)
{
	if (gamestate == GS_TITLELEVEL)
	{
		return;
	}
	if (dmflags & DF_NO_FREELOOK)
	{
		LocalViewPitch = 0;
	}
	else
	{
		LocalViewPitch += look << 16;
	}
	if (look != 0)
	{
		LocalKeyboardTurner = smooth_mouse;
	}
}

void G_AddViewAngle (int yaw)
{
	if (gamestate == GS_TITLELEVEL)
	{
		return;
	}
	LocalViewAngle -= yaw << 16;
	if (yaw != 0)
	{
		LocalKeyboardTurner = smooth_mouse;
	}
}

CVAR (Bool, bot_allowspy, false, 0)

// [RH] Spy mode has been separated into two console commands.
//		One goes forward; the other goes backward.
static void ChangeSpy (bool forward)
{
	// If you're not in a level, then you can't spy.
	if (gamestate != GS_LEVEL)
	{
		return;
	}

	// If not viewing through a player, return your eyes to your own head.
	if (players[consoleplayer].camera->player == NULL)
	{
		players[consoleplayer].camera = players[consoleplayer].mo;
		return;
	}

	// Otherwise, cycle to the next player.
	bool checkTeam = !demoplayback && deathmatch;
	int pnum = players[consoleplayer].camera->player - players;
	int step = forward ? 1 : -1;

	do
	{
		pnum += step;
		pnum &= MAXPLAYERS-1;
		if (playeringame[pnum] &&
			(!checkTeam || players[pnum].mo->IsTeammate (players[consoleplayer].mo) ||
			(bot_allowspy && players[pnum].isbot)))
		{
			break;
		}
	} while (pnum != consoleplayer);

	players[consoleplayer].camera = players[pnum].mo;
	S_UpdateSounds(players[consoleplayer].camera);
	StatusBar->AttachToPlayer (&players[pnum]);
	if (demoplayback || multiplayer)
	{
		StatusBar->ShowPlayerName ();
	}
}

CCMD (spynext)
{
	// allow spy mode changes even during the demo
	ChangeSpy (true);
}

CCMD (spyprev)
{
	// allow spy mode changes even during the demo
	ChangeSpy (false);
}


//
// G_Responder
// Get info needed to make ticcmd_ts for the players.
//
BOOL G_Responder (event_t *ev)
{
	// any other key pops up menu if in demos
	// [RH] But only if the key isn't bound to a "special" command
	if (gameaction == ga_nothing && 
		(demoplayback || gamestate == GS_DEMOSCREEN || gamestate == GS_TITLELEVEL))
	{
		const char *cmd = C_GetBinding (ev->data1);

		if (ev->type == EV_KeyDown)
		{

			if (!cmd || (
				strnicmp (cmd, "menu_", 5) &&
				stricmp (cmd, "toggleconsole") &&
				stricmp (cmd, "sizeup") &&
				stricmp (cmd, "sizedown") &&
				stricmp (cmd, "togglemap") &&
				stricmp (cmd, "spynext") &&
				stricmp (cmd, "spyprev") &&
				stricmp (cmd, "chase") &&
				stricmp (cmd, "+showscores") &&
				stricmp (cmd, "bumpgamma") &&
				stricmp (cmd, "screenshot")))
			{
				M_StartControlPanel (true);
				return true;
			}
			else
			{
				return C_DoKey (ev);
			}
		}
		if (cmd && cmd[0] == '+')
			return C_DoKey (ev);

		return false;
	}

	if (CT_Responder (ev))
		return true;			// chat ate the event

	if (gamestate == GS_LEVEL)
	{
		if (ST_Responder (ev))
			return true;		// status window ate it
		if (!viewactive && AM_Responder (ev))
			return true;		// automap ate it
	}
	else if (gamestate == GS_FINALE)
	{
		if (F_Responder (ev))
			return true;		// finale ate the event
	}

	switch (ev->type)
	{
	case EV_KeyDown:
		if (C_DoKey (ev))
			return true;
		break;

	case EV_KeyUp:
		C_DoKey (ev);
		break;

	// [RH] mouse buttons are sent as key up/down events
	case EV_Mouse: 
		mousex = (int)(ev->x * mouse_sensitivity);
		mousey = (int)(ev->y * mouse_sensitivity);
		break;
	}

	// [RH] If the view is active, give the automap a chance at
	// the events *last* so that any bound keys get precedence.

	if (gamestate == GS_LEVEL && viewactive)
		return AM_Responder (ev);

	return (ev->type == EV_KeyDown ||
			ev->type == EV_Mouse);
}



//
// G_Ticker
// Make ticcmd_ts for the players.
//
extern FTexture *Page;


void G_Ticker ()
{
	int i;
	gamestate_t	oldgamestate;

	// do player reborns if needed
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] &&
			(players[i].playerstate == PST_REBORN || players[i].playerstate == PST_ENTER))
		{
			G_DoReborn (i, false);
		}
	}

	if (ToggleFullscreen)
	{
		ToggleFullscreen = false;
		AddCommandString ("toggle fullscreen");
	}

	// do things to change the game state
	oldgamestate = gamestate;
	while (gameaction != ga_nothing)
	{
		if (gameaction == ga_newgame2)
		{
			gameaction = ga_newgame;
			break;
		}
		switch (gameaction)
		{
		case ga_loadlevel:
			G_DoLoadLevel (-1, false);
			break;
		case ga_newgame2:	// Silence GCC (see above)
		case ga_newgame:
			G_DoNewGame ();
			break;
		case ga_loadgame:
			G_DoLoadGame ();
			break;
		case ga_savegame:
			G_DoSaveGame (true);
			break;
		case ga_autosave:
			G_DoAutoSave ();
			break;
		case ga_playdemo:
			G_DoPlayDemo ();
			break;
		case ga_completed:
			G_DoCompleted ();
			break;
		case ga_slideshow:
			F_StartSlideshow ();
			break;
		case ga_worlddone:
			G_DoWorldDone ();
			break;
		case ga_screenshot:
			M_ScreenShot (shotfile);
			if (shotfile)
			{
				free (shotfile);
				shotfile = NULL;
			}
			gameaction = ga_nothing;
			break;
		case ga_fullconsole:
			C_FullConsole ();
			gameaction = ga_nothing;
			break;
		case ga_togglemap:
			AM_ToggleMap ();
			gameaction = ga_nothing;
			break;
		case ga_nothing:
			break;
		}
		C_AdjustBottom ();
	}

	if (oldgamestate != gamestate)
	{
		if (oldgamestate == GS_DEMOSCREEN && Page != NULL)
		{
			Page->Unload();
			Page = NULL;
		}
		else if (oldgamestate == GS_FINALE)
		{
			F_EndFinale ();
		}
	}

	// get commands, check consistancy, and build new consistancy check
	int buf = (gametic/ticdup)%BACKUPTICS;

	// [RH] Include some random seeds and player stuff in the consistancy
	// check, not just the player's x position like BOOM.
	DWORD rngsum = FRandom::StaticSumSeeds ();

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			ticcmd_t *cmd = &players[i].cmd;
			ticcmd_t *newcmd = &netcmds[i][buf];

			if ((gametic % ticdup) == 0)
			{
				RunNetSpecs (i, buf);
			}
			if (demorecording)
			{
				G_WriteDemoTiccmd (newcmd, i, buf);
			}
			// If the user alt-tabbed away, paused gets set to -1. In this case,
			// we do not want to read more demo commands until paused is no
			// longer negative.
			if (demoplayback && paused >= 0)
			{
				G_ReadDemoTiccmd (cmd, i);
			}
			else
			{
				memcpy (cmd, newcmd, sizeof(ticcmd_t));
			}

			// check for turbo cheats
			if (cmd->ucmd.forwardmove > TURBOTHRESHOLD &&
				!(gametic&31) && ((gametic>>5)&(MAXPLAYERS-1)) == i )
			{
				Printf ("%s is turbo!\n", players[i].userinfo.netname);
			}

			if (netgame && !players[i].isbot && !netdemo && (gametic%ticdup) == 0)
			{
				//players[i].inconsistant = 0;
				if (gametic > BACKUPTICS*ticdup && consistancy[i][buf] != cmd->consistancy)
				{
					players[i].inconsistant = gametic - BACKUPTICS*ticdup;
				}
				if (players[i].mo)
				{
					DWORD sum = rngsum + players[i].mo->x + players[i].mo->y + players[i].mo->z
						+ players[i].mo->angle + players[i].mo->pitch;
					sum ^= players[i].health;
					consistancy[i][buf] = sum;
				}
				else
				{
					consistancy[i][buf] = rngsum;
				}
			}
		}
	}

	// do main actions
	switch (gamestate)
	{
	case GS_LEVEL:
		P_Ticker ();
		AM_Ticker ();
		break;

	case GS_TITLELEVEL:
		P_Ticker ();
		break;

	case GS_INTERMISSION:
		WI_Ticker ();
		break;

	case GS_FINALE:
		F_Ticker ();
		break;

	case GS_DEMOSCREEN:
		D_PageTicker ();
		break;

	case GS_STARTUP:
		if (gameaction == ga_nothing)
		{
			gamestate = GS_FULLCONSOLE;
			gameaction = ga_fullconsole;
		}
		break;

	default:
		break;
	}
}


//
// PLAYER STRUCTURE FUNCTIONS
// also see P_SpawnPlayer in P_Mobj
//

//
// G_PlayerFinishLevel
// Called when a player completes a level.
//
void G_PlayerFinishLevel (int player, EFinishLevelType mode)
{
	AInventory *item, *next;
	player_t *p;

	p = &players[player];

	// Strip all current powers, unless moving in a hub and the power is okay to keep.
	item = p->mo->Inventory;
	while (item != NULL)
	{
		next = item->Inventory;
		if (item->IsKindOf (RUNTIME_CLASS(APowerup)))
		{
			if (deathmatch || mode != FINISH_SameHub || !(item->ItemFlags & IF_HUBPOWER))
			{
				item->Destroy ();
			}
		}
		item = next;
	}
	if (p->ReadyWeapon != NULL &&
		p->ReadyWeapon->WeaponFlags&WIF_POWERED_UP &&
		p->PendingWeapon == p->ReadyWeapon->SisterWeapon)
	{
		// Unselect powered up weapons if the unpowered counterpart is pending
		p->ReadyWeapon=p->PendingWeapon;
	}
	p->mo->flags &= ~MF_SHADOW; 		// cancel invisibility
	p->mo->RenderStyle = STYLE_Normal;
	p->mo->alpha = FRACUNIT;
	p->extralight = 0;					// cancel gun flashes
	p->fixedcolormap = 0;				// cancel ir goggles
	p->damagecount = 0; 				// no palette changes
	p->bonuscount = 0;
	p->poisoncount = 0;
	p->inventorytics = 0;

	if (mode != FINISH_SameHub)
	{
		// Take away flight and keys (and anything else with IF_INTERHUBSTRIP set)
		item = p->mo->Inventory;
		while (item != NULL)
		{
			next = item->Inventory;
			if (item->ItemFlags & IF_INTERHUBSTRIP)
			{
				item->Destroy ();
			}
			item = next;
		}
	}

	if (mode == FINISH_NoHub && !(level.flags & LEVEL_KEEPFULLINVENTORY))
	{ // Reduce all owned (visible) inventory to 1 item each
		for (item = p->mo->Inventory; item != NULL; item = item->Inventory)
		{
			if (item->ItemFlags & IF_INVBAR)
			{
				item->Amount = 1;
			}
		}
	}

	if (p->morphTics)
	{ // Undo morph
		P_UndoPlayerMorph (p, true);
	}
}


//
// G_PlayerReborn
// Called after a player dies
// almost everything is cleared and initialized
//
void G_PlayerReborn (int player)
{
	player_t*	p;
	int 		frags[MAXPLAYERS];
	int			fragcount;	// [RH] Cumulative frags
	int 		killcount;
	int 		itemcount;
	int 		secretcount;
	BYTE		currclass;
	userinfo_t  userinfo;	// [RH] Save userinfo
	botskill_t  b_skill;//Added by MC:
	APlayerPawn *actor;
	const PClass *cls;
	FString		log;

	p = &players[player];

	memcpy (frags, p->frags, sizeof(frags));
	fragcount = p->fragcount;
	killcount = p->killcount;
	itemcount = p->itemcount;
	secretcount = p->secretcount;
	currclass = p->CurrentPlayerClass;
    b_skill = p->skill;    //Added by MC:
	memcpy (&userinfo, &p->userinfo, sizeof(userinfo));
	actor = p->mo;
	cls = p->cls;
	log = p->LogText;

	// Reset player structure to its defaults
	p->~player_t();
	::new(p) player_t;

	memcpy (p->frags, frags, sizeof(p->frags));
	p->fragcount = fragcount;
	p->killcount = killcount;
	p->itemcount = itemcount;
	p->secretcount = secretcount;
	p->CurrentPlayerClass = currclass;
	memcpy (&p->userinfo, &userinfo, sizeof(userinfo));
	p->mo = actor;
	p->cls = cls;
	p->LogText = log;

    p->skill = b_skill;	//Added by MC:

	p->oldbuttons = 255, p->attackdown = true;	// don't do anything immediately
	p->playerstate = PST_LIVE;

	if (gamestate != GS_TITLELEVEL)
	{
		actor->GiveDefaultInventory ();
		p->ReadyWeapon = p->PendingWeapon;
	}

    //Added by MC: Init bot structure.
    if (bglobal.botingame[player])
        bglobal.CleanBotstuff (p);
    else
		p->isbot = false;

	// [BC] Handle temporary invulnerability when respawned
	if ((dmflags2 & DF2_YES_INVUL) && (deathmatch || alwaysapplydmflags))
	{
		APowerup *invul = static_cast<APowerup*>(actor->GiveInventoryType (RUNTIME_CLASS(APowerInvulnerable)));
		invul->EffectTics = 2*TICRATE;
		invul->BlendColor = 0;				// don't mess with the view
		actor->effects |= FX_RESPAWNINVUL;	// [RH] special effect
	}
}

//
// G_CheckSpot	
// Returns false if the player cannot be respawned
// at the given mapthing2_t spot  
// because something is occupying it 
//

BOOL G_CheckSpot (int playernum, mapthing2_t *mthing)
{
	fixed_t x;
	fixed_t y;
	fixed_t z, oldz;
	int i;

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;
	z = mthing->z << FRACBITS;

	z += R_PointInSubsector (x, y)->sector->floorplane.ZatPoint (x, y);

	if (!players[playernum].mo)
	{ // first spawn of level, before corpses
		for (i = 0; i < playernum; i++)
			if (players[i].mo && players[i].mo->x == x && players[i].mo->y == y)
				return false;
		return true;
	}

	oldz = players[playernum].mo->z;	// [RH] Need to save corpse's z-height
	players[playernum].mo->z = z;		// [RH] Checks are now full 3-D

	// killough 4/2/98: fix bug where P_CheckPosition() uses a non-solid
	// corpse to detect collisions with other players in DM starts
	//
	// Old code:
	// if (!P_CheckPosition (players[playernum].mo, x, y))
	//    return false;

	players[playernum].mo->flags |=  MF_SOLID;
	i = P_CheckPosition(players[playernum].mo, x, y);
	players[playernum].mo->flags &= ~MF_SOLID;
	players[playernum].mo->z = oldz;	// [RH] Restore corpse's height
	if (!i)
		return false;

	return true;
}


//
// G_DeathMatchSpawnPlayer 
// Spawns a player at one of the random death match spots 
// called at level load and each death 
//

// [RH] Returns the distance of the closest player to the given mapthing2_t.
static fixed_t PlayersRangeFromSpot (mapthing2_t *spot)
{
	fixed_t closest = INT_MAX;
	fixed_t distance;
	int i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || !players[i].mo || players[i].health <= 0)
			continue;

		distance = P_AproxDistance (players[i].mo->x - spot->x * FRACUNIT,
									players[i].mo->y - spot->y * FRACUNIT);

		if (distance < closest)
			closest = distance;
	}

	return closest;
}

// [RH] Select the deathmatch spawn spot farthest from everyone.
static mapthing2_t *SelectFarthestDeathmatchSpot (size_t selections)
{
	fixed_t bestdistance = 0;
	mapthing2_t *bestspot = NULL;
	unsigned int i;

	for (i = 0; i < selections; i++)
	{
		fixed_t distance = PlayersRangeFromSpot (&deathmatchstarts[i]);

		if (distance > bestdistance)
		{
			bestdistance = distance;
			bestspot = &deathmatchstarts[i];
		}
	}

	return bestspot;
}

// [RH] Select a deathmatch spawn spot at random (original mechanism)
static mapthing2_t *SelectRandomDeathmatchSpot (int playernum, unsigned int selections)
{
	unsigned int i, j;

	for (j = 0; j < 20; j++)
	{
		i = pr_dmspawn() % selections;
		if (G_CheckSpot (playernum, &deathmatchstarts[i]) )
		{
			return &deathmatchstarts[i];
		}
	}

	// [RH] return a spot anyway, since we allow telefragging when a player spawns
	return &deathmatchstarts[i];
}

void G_DeathMatchSpawnPlayer (int playernum)
{
	unsigned int selections;
	mapthing2_t *spot;

	selections = deathmatchstarts.Size ();
	// [RH] We can get by with just 1 deathmatch start
	if (selections < 1)
		I_Error ("No deathmatch starts");

	// At level start, none of the players have mobjs attached to them,
	// so we always use the random deathmatch spawn. During the game,
	// though, we use whatever dmflags specifies.
	if ((dmflags & DF_SPAWN_FARTHEST) && players[playernum].mo)
		spot = SelectFarthestDeathmatchSpot (selections);
	else
		spot = SelectRandomDeathmatchSpot (playernum, selections);

	if (!spot)
	{ // no good spot, so the player will probably get stuck
		spot = &playerstarts[playernum];
	}
	else
	{
		if (playernum < 4)
			spot->type = playernum+1;
		else if (gameinfo.gametype != GAME_Hexen)
			spot->type = playernum+4001-4;	// [RH] > 4 players
		else
			spot->type = playernum+9100-4;
	}

	P_SpawnPlayer (spot);
}

//
// G_QueueBody
//
static void G_QueueBody (AActor *body)
{
	// flush an old corpse if needed
	int modslot = bodyqueslot%BODYQUESIZE;

	if (bodyqueslot >= BODYQUESIZE && bodyque[modslot] != NULL)
	{
		bodyque[modslot]->Destroy ();
	}
	bodyque[modslot] = body;

	// Copy the player's translation in case they respawn as something that uses
	// a different translation range.
	R_CopyTranslation (TRANSLATION(TRANSLATION_PlayerCorpses,modslot), body->Translation);
	body->Translation = TRANSLATION(TRANSLATION_PlayerCorpses,modslot);

	bodyqueslot++;
}

//
// G_DoReborn
//
void G_DoReborn (int playernum, bool freshbot)
{
	if (!multiplayer)
	{
		if (BackupSaveName.Len() > 0 && FileExists (BackupSaveName.GetChars()))
		{ // Load game from the last point it was saved
			savename = BackupSaveName;
			gameaction = ga_loadgame;
		}
		else
		{ // Reload the level from scratch
			bool indemo = demoplayback;
			BackupSaveName = "";
			G_InitNew (level.mapname, false);
			demoplayback = indemo;
//			gameaction = ga_loadlevel;
		}
	}
	else
	{
		// respawn at the start
		int i;

		// first disassociate the corpse
		if (players[playernum].mo)
		{
			G_QueueBody (players[playernum].mo);
			players[playernum].mo->player = NULL;
		}

		// spawn at random spot if in death match
		if (deathmatch)
		{
			G_DeathMatchSpawnPlayer (playernum);
			return;
		}

		if (G_CheckSpot (playernum, &playerstarts[playernum]) )
		{
			P_SpawnPlayer (&playerstarts[playernum]);
		}
		else
		{
			// try to spawn at one of the other players' spots
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (G_CheckSpot (playernum, &playerstarts[i]) )
				{
					int oldtype = playerstarts[i].type;

					// fake as other player
					// [RH] These numbers should be common across all games. Or better yet, not
					// used at all outside P_SpawnMapThing().
					if (playernum < 4 || gameinfo.gametype == GAME_Strife)
					{
						playerstarts[i].type = playernum + 1;
					}
					else if (gameinfo.gametype == GAME_Hexen)
					{
						playerstarts[i].type = playernum + 9100 - 4;
					}
					else
					{
						playerstarts[i].type = playernum + 4001 - 4;
					}
					P_SpawnPlayer (&playerstarts[i]);
					playerstarts[i].type = oldtype; 			// restore 
					return;
				}
				// he's going to be inside something.  Too bad.
			}
			P_SpawnPlayer (&playerstarts[playernum]);
		}
	}
}

void G_ScreenShot (char *filename)
{
	shotfile = filename;
	gameaction = ga_screenshot;
}





//
// G_InitFromSavegame
// Can be called by the startup code or the menu task.
//
void G_LoadGame (const char* name)
{
	if (name != NULL)
	{
		savename = name;
		gameaction = ga_loadgame;
	}
}

static bool CheckSingleWad (char *name, bool &printRequires, bool printwarn)
{
	if (name == NULL)
	{
		return true;
	}
	if (Wads.CheckIfWadLoaded (name) < 0)
	{
		if (printwarn)
		{
			if (!printRequires)
			{
				Printf ("This savegame needs these wads:\n%s", name);
			}
			else
			{
				Printf (", %s", name);
			}
		}
		printRequires = true;
		delete[] name;
		return false;
	}
	delete[] name;
	return true;
}

// Return false if not all the needed wads have been loaded.
bool G_CheckSaveGameWads (PNGHandle *png, bool printwarn)
{
	char *text;
	bool printRequires = false;

	text = M_GetPNGText (png, "Game WAD");
	CheckSingleWad (text, printRequires, printwarn);
	text = M_GetPNGText (png, "Map WAD");
	CheckSingleWad (text, printRequires, printwarn);

	if (printRequires)
	{
		if (printwarn)
		{
			Printf ("\n");
		}
		return false;
	}

	return true;
}

static void ReadVars (PNGHandle *png, SDWORD *vars, size_t count, DWORD id)
{
	size_t len = M_FindPNGChunk (png, id);
	size_t used = 0;

	if (len != 0)
	{
		DWORD var;
		size_t i;
		FPNGChunkArchive arc (png->File->GetFile(), id, len);
		used = len / 4;

		for (i = 0; i < used; ++i)
		{
			arc << var;
			vars[i] = var;
		}
		png->File->ResetFilePtr();
	}
	if (used < count)
	{
		memset (&vars[used], 0, (count-used)*4);
	}
}

static void ReadArrayVars (PNGHandle *png, TArray<SDWORD> *vars, size_t count, DWORD id)
{
	size_t len = M_FindPNGChunk (png, id);
	unsigned int i, k;

	for (i = 0; i < count; ++i)
	{
		vars[i].Clear ();
	}

	if (len != 0)
	{
		DWORD max, size;
		DWORD var;
		FPNGChunkArchive arc (png->File->GetFile(), id, len);

		i = arc.ReadCount ();
		max = arc.ReadCount ();

		for (; i <= max; ++i)
		{
			size = arc.ReadCount ();
			if (size > 0)
			{
				vars[i].Resize (size);
			}
			for (k = 0; k < size; ++k)
			{
				arc << var;
				vars[i][k] = var;
			}
		}
		png->File->ResetFilePtr();
	}
}

void G_DoLoadGame ()
{
	char sigcheck[16];
	char *text = NULL;
	char *map;

	gameaction = ga_nothing;

	FILE *stdfile = fopen (savename.GetChars(), "rb");
	if (stdfile == NULL)
	{
		Printf ("Could not read savegame '%s'\n", savename.GetChars());
		return;
	}

	PNGHandle *png = M_VerifyPNG (stdfile);
	if (png == NULL)
	{
		fclose (stdfile);
		Printf ("'%s' is not a valid (PNG) savegame\n", savename.GetChars());
		return;
	}

	SaveVersion = 0;

	// Check whether this savegame actually has been created by a compatible engine.
	// Since there are ZDoom derivates using the exact same savegame format but
	// with mutual incompatibilities this check simplifies things significantly.
	char *engine = M_GetPNGText (png, "Engine");
	if (engine == NULL || 0 != strcmp (engine, GAMESIG))
	{
		// Make a special case for the message printed for old savegames that don't
		// have this information.
		if (engine == NULL)
		{
			Printf ("Savegame is from an incompatible version\n");
		}
		else
		{
			Printf ("Savegame is from another ZDoom-based engine: %s\n", engine);
			delete[] engine;
		}
		delete png;
		fclose (stdfile);
		return;
	}
	if (engine != NULL)
	{
		delete[] engine;
	}

	if (!M_GetPNGText (png, "ZDoom Save Version", sigcheck, 16) ||
		0 != strncmp (sigcheck, SAVESIG, 9) ||		// ZDOOMSAVE is the first 9 chars
		(SaveVersion = atoi (sigcheck+9)) < MINSAVEVER)
	{
		Printf ("Savegame is from an incompatible version\n");
		delete png;
		fclose (stdfile);
		return;
	}

	if (!G_CheckSaveGameWads (png, true))
	{
		fclose (stdfile);
		return;
	}

	map = M_GetPNGText (png, "Current Map");
	if (map == NULL)
	{
		Printf ("Savegame is missing the current map\n");
		fclose (stdfile);
		return;
	}

	// Read intermission data for hubs
	G_ReadHubInfo(png);

	bglobal.RemoveAllBots (true);

	text = M_GetPNGText (png, "Important CVARs");
	if (text != NULL)
	{
		byte *vars_p = (byte *)text;
		C_ReadCVars (&vars_p);
		delete[] text;
	}

	// dearchive all the modifications
	if (M_FindPNGChunk (png, MAKE_ID('p','t','I','c')) == 8)
	{
		DWORD time[2];
		fread (&time, 8, 1, stdfile);
		time[0] = BigLong((unsigned int)time[0]);
		time[1] = BigLong((unsigned int)time[1]);
		level.time = Scale (time[1], TICRATE, time[0]);
	}
	else
	{ // No ptIc chunk so we don't know how long the user was playing
		level.time = 0;
	}

	G_ReadSnapshots (png);
	FRandom::StaticReadRNGState (png);
	P_ReadACSDefereds (png);

	// load a base level
	savegamerestore = true;		// Use the player actors in the savegame
	G_InitNew (map, false);
	delete[] map;
	savegamerestore = false;

	ReadVars (png, ACS_WorldVars, NUM_WORLDVARS, MAKE_ID('w','v','A','r'));
	ReadVars (png, ACS_GlobalVars, NUM_GLOBALVARS, MAKE_ID('g','v','A','r'));
	ReadArrayVars (png, ACS_WorldArrays, NUM_WORLDVARS, MAKE_ID('w','a','R','r'));
	ReadArrayVars (png, ACS_GlobalArrays, NUM_GLOBALVARS, MAKE_ID('g','a','R','r'));

	NextSkill = -1;
	if (M_FindPNGChunk (png, MAKE_ID('s','n','X','t')) == 1)
	{
		BYTE next;
		fread (&next, 1, 1, stdfile);
		NextSkill = next;
	}

	if (level.info->snapshot != NULL)
	{
		delete level.info->snapshot;
		level.info->snapshot = NULL;
	}

	BackupSaveName = savename;

	delete png;
	fclose (stdfile);
}


//
// G_SaveGame
// Called by the menu task.
// Description is a 24 byte text string
//
void G_SaveGame (const char *filename, const char *description)
{
	savegamefile = filename;
	strcpy (savedescription, description);
	sendsave = true;
}

FString G_BuildSaveName (const char *prefix, int slot)
{
	FString name;
	const char *leader;
	const char *slash = "";

	if (NULL != (leader = Args.CheckValue ("-savedir")))
	{
		size_t len = strlen (leader);
		if (leader[len-1] != '\\' && leader[len-1] != '/')
		{
			slash = "/";
		}
	}
#ifndef unix
	else if (Args.CheckParm ("-cdrom"))
	{
		leader = "c:/zdoomdat/";
	}
	else
	{
		leader = progdir;
	}
#else
	else
	{
		leader = "";
	}
#endif
	if (slot < 0)
	{
		name.Format ("%s%s%s", leader, slash, prefix);
	}
	else
	{
		name.Format ("%s%s%s%d.zds", leader, slash, prefix, slot);
	}
#ifdef unix
	if (leader[0] == 0)
	{
		name = GetUserFile (name);
	}
#endif
	return name;
}

CVAR (Int, autosavenum, 0, CVAR_NOSET|CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Int, disableautosave, 1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CUSTOM_CVAR (Int, autosavecount, 4, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 0)
		self = 0;
	if (self > 20)
		self = 20;
}

extern void P_CalcHeight (player_t *);

void G_DoAutoSave ()
{
	// Do not autosave in multiplayer games or demos or when dead
	if (multiplayer ||
		demoplayback ||
		players[consoleplayer].playerstate != PST_LIVE ||
		disableautosave >= 2 ||
		autosavecount == 0)
	{
		gameaction = ga_nothing;
		return;
	}

	// Keep up to four autosaves at a time
	UCVarValue num;
	char *readableTime;
	
	num.Int = (autosavenum + 1) % autosavecount;
	autosavenum.ForceSet (num, CVAR_Int);

	savegamefile = G_BuildSaveName ("auto", num.Int);

	readableTime = myasctime ();
	strcpy (savedescription, "Autosave ");
	strncpy (savedescription+9, readableTime+4, 12);
	savedescription[9+12] = 0;

	G_DoSaveGame (false);
}


static void PutSaveWads (FILE *file)
{
	const char *name;

	// Name of IWAD
	name = Wads.GetWadName (FWadCollection::IWAD_FILENUM);
	M_AppendPNGText (file, "Game WAD", name);

	// Name of wad the map resides in
	if (Wads.GetLumpFile (level.lumpnum) > 1)
	{
		name = Wads.GetWadName (Wads.GetLumpFile (level.lumpnum));
		M_AppendPNGText (file, "Map WAD", name);
	}
}

static void PutSaveComment (FILE *file)
{
	char comment[256];
	char *readableTime;
	WORD len;
	int levelTime;

	// Get the current date and time
	readableTime = myasctime ();

	strncpy (comment, readableTime, 10);
	strncpy (comment+10, readableTime+19, 5);
	strncpy (comment+15, readableTime+10, 9);
	comment[24] = 0;

	M_AppendPNGText (file, "Creation Time", comment);

	// Get level name
	//strcpy (comment, level.level_name);
	sprintf(comment, "%s - %s", level.mapname, level.level_name);
	len = (WORD)strlen (comment);
	comment[len] = '\n';

	// Append elapsed time
	levelTime = level.time / TICRATE;
	sprintf (comment+len+1, "time: %02d:%02d:%02d",
		levelTime/3600, (levelTime%3600)/60, levelTime%60);
	comment[len+16] = 0;

	// Write out the comment
	M_AppendPNGText (file, "Comment", comment);
}

static void PutSavePic (FILE *file, int width, int height)
{
	if (width <= 0 || height <= 0 || !storesavepic)
	{
		M_CreateDummyPNG (file);
	}
	else
	{
		DCanvas *pic = new DSimpleCanvas (width, height);
		PalEntry palette[256];

		// Take a snapshot of the player's view
		pic->Lock ();
		P_CheckPlayerSprites();
		R_RenderViewToCanvas (players[consoleplayer].mo, pic, 0, 0, width, height);
		screen->GetFlashedPalette (palette);
		M_CreatePNG (file, pic, palette);
		pic->Unlock ();
		delete pic;
	}
}

static void WriteVars (FILE *file, SDWORD *vars, size_t count, DWORD id)
{
	size_t i, j;

	for (i = 0; i < count; ++i)
	{
		if (vars[i] != 0)
			break;
	}
	if (i < count)
	{
		// Find last non-zero var. Anything beyond the last stored variable
		// will be zeroed at load time.
		for (j = count-1; j > i; --j)
		{
			if (vars[j] != 0)
				break;
		}
		FPNGChunkArchive arc (file, id);
		for (i = 0; i <= j; ++i)
		{
			DWORD var = vars[i];
			arc << var;
		}
	}
}

static void WriteArrayVars (FILE *file, TArray<SDWORD> *vars, unsigned int count, DWORD id)
{
	unsigned int i, j, k;
	SDWORD val;

	for (i = 0; i < count; ++i)
	{
		if (vars[i].Size() != 0)
			break;
	}
	if (i < count)
	{
		// Find last non-empty array. Anything beyond the last stored array
		// will be emptied at load time.
		for (j = count-1; j > i; --j)
		{
			if (vars[j].Size() != 0)
				break;
		}
		FPNGChunkArchive arc (file, id);
		arc.WriteCount (i);
		arc.WriteCount (j);
		for (; i <= j; ++i)
		{
			arc.WriteCount (vars[i].Size());
			for (k = 0; k < vars[i].Size(); ++k)
			{
				val = vars[i][k];
				arc << val;
			}
		}
	}
}

void G_DoSaveGame (bool okForQuicksave)
{
	if (demoplayback)
	{
		gameaction = ga_nothing;
		return;
	}

	insave=true;
	G_SnapshotLevel ();

	FILE *stdfile = fopen (savegamefile.GetChars(), "wb");

	if (stdfile == NULL)
	{
		Printf ("Could not create savegame '%s'\n", savegamefile.GetChars());
		savegamefile = "";
		gameaction = ga_nothing;
		insave = false;
		return;
	}

	SaveVersion = SAVEVER;
	PutSavePic (stdfile, SAVEPICWIDTH, SAVEPICHEIGHT);
	M_AppendPNGText (stdfile, "Software", "ZDoom " DOTVERSIONSTR);
	M_AppendPNGText (stdfile, "Engine", GAMESIG);
	M_AppendPNGText (stdfile, "ZDoom Save Version", SAVESIG);
	M_AppendPNGText (stdfile, "Title", savedescription);
	M_AppendPNGText (stdfile, "Current Map", level.mapname);
	PutSaveWads (stdfile);
	PutSaveComment (stdfile);

	// Intermission stats for hubs
	G_WriteHubInfo(stdfile);

	{
		byte vars[4096], *vars_p;
		vars_p = vars;
		C_WriteCVars (&vars_p, CVAR_SERVERINFO);
		*vars_p = 0;
		M_AppendPNGText (stdfile, "Important CVARs", (char *)vars);
	}

	if (level.time != 0 || level.maptime != 0)
	{
		DWORD time[2] = { BigLong(TICRATE), BigLong(level.time) };
		M_AppendPNGChunk (stdfile, MAKE_ID('p','t','I','c'), (BYTE *)&time, 8);
	}

	G_WriteSnapshots (stdfile);
	FRandom::StaticWriteRNGState (stdfile);
	P_WriteACSDefereds (stdfile);

	WriteVars (stdfile, ACS_WorldVars, NUM_WORLDVARS, MAKE_ID('w','v','A','r'));
	WriteVars (stdfile, ACS_GlobalVars, NUM_GLOBALVARS, MAKE_ID('g','v','A','r'));
	WriteArrayVars (stdfile, ACS_WorldArrays, NUM_WORLDVARS, MAKE_ID('w','a','R','r'));
	WriteArrayVars (stdfile, ACS_GlobalArrays, NUM_GLOBALVARS, MAKE_ID('g','a','R','r'));

	if (NextSkill != -1)
	{
		BYTE next = NextSkill;
		M_AppendPNGChunk (stdfile, MAKE_ID('s','n','X','t'), &next, 1);
	}

	M_NotifyNewSave (savegamefile.GetChars(), savedescription, okForQuicksave);
	gameaction = ga_nothing;
	savedescription[0] = 0;

	M_FinishPNG (stdfile);
	fclose (stdfile);

	// Check whether the file is ok.
	bool success = false;
	stdfile = fopen (savegamefile.GetChars(), "rb");
	if (stdfile != NULL)
	{
		PNGHandle * pngh = M_VerifyPNG(stdfile);
		if (pngh != NULL)
		{
			success=true;
			delete pngh;
		}
		fclose(stdfile);
	}
	if (success) Printf ("%s\n", GStrings("GGSAVED"));
	else Printf(PRINT_HIGH, "Save failed\n");

	BackupSaveName = savegamefile;
	savegamefile = "";

	// We don't need the snapshot any longer.
	if (level.info->snapshot != NULL)
	{
		delete level.info->snapshot;
		level.info->snapshot = NULL;
	}
		
	insave = false;
}




//
// DEMO RECORDING
//

void G_ReadDemoTiccmd (ticcmd_t *cmd, int player)
{
	int id = DEM_BAD;

	while (id != DEM_USERCMD && id != DEM_EMPTYUSERCMD)
	{
		if (!demorecording && demo_p >= zdembodyend)
		{
			// nothing left in the BODY chunk, so end playback.
			G_CheckDemoStatus ();
			break;
		}

		id = ReadByte (&demo_p);

		switch (id)
		{
		case DEM_STOP:
			// end of demo stream
			G_CheckDemoStatus ();
			break;

		case DEM_USERCMD:
			UnpackUserCmd (&cmd->ucmd, &cmd->ucmd, &demo_p);
			break;

		case DEM_EMPTYUSERCMD:
			// leave cmd->ucmd unchanged
			break;

		case DEM_DROPPLAYER:
			{
				byte i = ReadByte (&demo_p);
				if (i < MAXPLAYERS)
					playeringame[i] = false;
			}
			break;

		default:
			Net_DoCommand (id, &demo_p, player);
			break;
		}
	}
} 

BOOL stoprecording;

CCMD (stop)
{
	stoprecording = true;
}

extern byte *lenspot;

void G_WriteDemoTiccmd (ticcmd_t *cmd, int player, int buf)
{
	byte *specdata;
	int speclen;

	if (stoprecording)
	{ // use "stop" console command to end demo recording
		G_CheckDemoStatus ();
		if (!netgame)
		{
			gameaction = ga_fullconsole;
		}
		return;
	}

	// [RH] Write any special "ticcmds" for this player to the demo
	if ((specdata = NetSpecs[player][buf].GetData (&speclen)) && gametic % ticdup == 0)
	{
		memcpy (demo_p, specdata, speclen);
		demo_p += speclen;
		NetSpecs[player][buf].SetData (NULL, 0);
	}

	// [RH] Now write out a "normal" ticcmd.
	WriteUserCmdMessage (&cmd->ucmd, &players[player].cmd.ucmd, &demo_p);

	// [RH] Bigger safety margin
	if (demo_p > demobuffer + maxdemosize - 64)
	{
		ptrdiff_t pos = demo_p - demobuffer;
		ptrdiff_t spot = lenspot - demobuffer;
		ptrdiff_t comp = democompspot - demobuffer;
		ptrdiff_t body = demobodyspot - demobuffer;
		// [RH] Allocate more space for the demo
		maxdemosize += 0x20000;
		demobuffer = (byte *)M_Realloc (demobuffer, maxdemosize);
		demo_p = demobuffer + pos;
		lenspot = demobuffer + spot;
		democompspot = demobuffer + comp;
		demobodyspot = demobuffer + body;
	}
}



//
// G_RecordDemo
//
void G_RecordDemo (char* name)
{
	char *v;

	usergame = false;
	strcpy (demoname, name);
	FixPathSeperator (demoname);
	DefaultExtension (demoname, ".lmp");
	v = Args.CheckValue ("-maxdemo");
	maxdemosize = 0x20000;
	demobuffer = (byte *)M_Malloc (maxdemosize);

	demorecording = true; 
}


// [RH] Demos are now saved as IFF FORMs. I've also removed support
//		for earlier ZDEMs since I didn't want to bother supporting
//		something that probably wasn't used much (if at all).

void G_BeginRecording (const char *startmap)
{
	int i;

	if (startmap == NULL)
	{
		startmap = level.mapname;
	}
	demo_p = demobuffer;

	WriteLong (FORM_ID, &demo_p);			// Write FORM ID
	demo_p += 4;							// Leave space for len
	WriteLong (ZDEM_ID, &demo_p);			// Write ZDEM ID

	// Write header chunk
	StartChunk (ZDHD_ID, &demo_p);
	WriteWord (DEMOGAMEVERSION, &demo_p);	// Write ZDoom version
	*demo_p++ = 2;							// Write minimum version needed to use this demo.
	*demo_p++ = 3;							// (Useful?)
	for (i = 0; i < 8; i++)					// Write name of map demo was recorded on.
	{
		*demo_p++ = startmap[i];
	}
	WriteLong (rngseed, &demo_p);			// Write RNG seed
	*demo_p++ = consoleplayer;
	FinishChunk (&demo_p);

	// Write player info chunks
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			StartChunk (UINF_ID, &demo_p);
			WriteByte ((byte)i, &demo_p);
			D_WriteUserInfoStrings (i, &demo_p);
			FinishChunk (&demo_p);
		}
	}

	// Write cvars chunk
	StartChunk (VARS_ID, &demo_p);
	C_WriteCVars (&demo_p, CVAR_SERVERINFO|CVAR_DEMOSAVE);
	FinishChunk (&demo_p);

	// Indicate body is compressed
	StartChunk (COMP_ID, &demo_p);
	democompspot = demo_p;
	WriteLong (0, &demo_p);
	FinishChunk (&demo_p);

	// Begin BODY chunk
	StartChunk (BODY_ID, &demo_p);
	demobodyspot = demo_p;
}


//
// G_PlayDemo
//

char defdemoname[128];

void G_DeferedPlayDemo (char *name)
{
	strncpy (defdemoname, name, 127);
	gameaction = ga_playdemo;
}

CCMD (playdemo)
{
	if (argv.argc() > 1)
	{
		G_DeferedPlayDemo (argv[1]);
		singledemo = true;
	}
}

CCMD (timedemo)
{
	if (argv.argc() > 1)
	{
		G_TimeDemo (argv[1]);
		singledemo = true;
	}
}

// [RH] Process all the information in a FORM ZDEM
//		until a BODY chunk is entered.
BOOL G_ProcessIFFDemo (char *mapname)
{
	BOOL headerHit = false;
	BOOL bodyHit = false;
	int numPlayers = 0;
	int id, len, i;
	uLong uncompSize = 0;
	byte *nextchunk;

	demoplayback = true;

	for (i = 0; i < MAXPLAYERS; i++)
		playeringame[i] = 0;

	len = ReadLong (&demo_p);
	zdemformend = demo_p + len + (len & 1);

	// Check to make sure this is a ZDEM chunk file.
	// TODO: Support multiple FORM ZDEMs in a CAT. Might be useful.

	id = ReadLong (&demo_p);
	if (id != ZDEM_ID)
	{
		Printf ("Not a ZDoom demo file!\n");
		return true;
	}

	// Process all chunks until a BODY chunk is encountered.

	while (demo_p < zdemformend && !bodyHit)
	{
		id = ReadLong (&demo_p);
		len = ReadLong (&demo_p);
		nextchunk = demo_p + len + (len & 1);
		if (nextchunk > zdemformend)
		{
			Printf ("Demo is mangled!\n");
			return true;
		}

		switch (id)
		{
		case ZDHD_ID:
			headerHit = true;

			demover = ReadWord (&demo_p);	// ZDoom version demo was created with
			if (demover < MINDEMOVERSION)
			{
				Printf ("Demo requires an older version of ZDoom!\n");
				//return true;
			}
			if (ReadWord (&demo_p) > DEMOGAMEVERSION)	// Minimum ZDoom version
			{
				Printf ("Demo requires a newer version of ZDoom!\n");
				return true;
			}
			memcpy (mapname, demo_p, 8);	// Read map name
			mapname[8] = 0;
			demo_p += 8;
			rngseed = ReadLong (&demo_p);
			FRandom::StaticClearRandom ();
			consoleplayer = *demo_p++;
			break;

		case VARS_ID:
			C_ReadCVars (&demo_p);
			break;

		case UINF_ID:
			i = ReadByte (&demo_p);
			if (!playeringame[i])
			{
				playeringame[i] = 1;
				numPlayers++;
			}
			D_ReadUserInfoStrings (i, &demo_p, false);
			break;

		case BODY_ID:
			bodyHit = true;
			zdembodyend = demo_p + len;
			break;

		case COMP_ID:
			uncompSize = ReadLong (&demo_p);
			break;
		}

		if (!bodyHit)
			demo_p = nextchunk;
	}

	if (!numPlayers)
	{
		Printf ("Demo has no players!\n");
		return true;
	}

	if (!bodyHit)
	{
		zdembodyend = NULL;
		Printf ("Demo has no BODY chunk!\n");
		return true;
	}

	if (numPlayers > 1)
		multiplayer = netgame = netdemo = true;

	if (uncompSize > 0)
	{
		BYTE *uncompressed = new BYTE[uncompSize];
		int r = uncompress (uncompressed, &uncompSize, demo_p, zdembodyend - demo_p);
		if (r != Z_OK)
		{
			Printf ("Could not decompress demo!\n");
			delete[] uncompressed;
			return true;
		}
		delete[] demobuffer;
		zdembodyend = uncompressed + uncompSize;
		demobuffer = demo_p = uncompressed;
	}

	return false;
}

void G_DoPlayDemo (void)
{
	char mapname[9];
	int demolump;

	gameaction = ga_nothing;

	// [RH] Allow for demos not loaded as lumps
	demolump = Wads.CheckNumForName (defdemoname);
	if (demolump >= 0)
	{
		int demolen = Wads.LumpLength (demolump);
		demobuffer = new byte[demolen];
		Wads.ReadLump (demolump, demobuffer);
	}
	else
	{
		FixPathSeperator (defdemoname);
		DefaultExtension (defdemoname, ".lmp");
		M_ReadFile (defdemoname, &demobuffer);
	}
	demo_p = demobuffer;

	Printf ("Playing demo %s\n", defdemoname);

	C_BackupCVars ();		// [RH] Save cvars that might be affected by demo

	if (ReadLong (&demo_p) != FORM_ID)
	{
		const char *eek = "Cannot play non-ZDoom demos.\n(They would go out of sync badly.)\n";

		if (singledemo)
		{
			I_Error (eek);
		}
		else
		{
			Printf (PRINT_BOLD, eek);
			gameaction = ga_nothing;
		}
	}
	else if (G_ProcessIFFDemo (mapname))
	{
		gameaction = ga_nothing;
		demoplayback = false;
	}
	else
	{
		// don't spend a lot of time in loadlevel 
		precache = false;
		demonew = true;
		G_InitNew (mapname, false);
		C_HideConsole ();
		demonew = false;
		precache = true;

		usergame = false;
		demoplayback = true;
	}
}

//
// G_TimeDemo
//
void G_TimeDemo (char* name)
{
	nodrawers = Args.CheckParm ("-nodraw");
	noblit = Args.CheckParm ("-noblit");
	timingdemo = true;
	singletics = true;

	strncpy (defdemoname, name, 128);
	gameaction = ga_playdemo;
}


/*
===================
=
= G_CheckDemoStatus
=
= Called after a death or level completion to allow demos to be cleaned up
= Returns true if a new demo loop action will take place
===================
*/

BOOL G_CheckDemoStatus (void)
{
	if (!demorecording)
	{ // [RH] Restore the player's userinfo settings.
		D_SetupUserInfo();
	}

	if (demoplayback)
	{
		extern int starttime;
		int endtime = 0;

		if (timingdemo)
			endtime = I_GetTime (false) - starttime;

		C_RestoreCVars ();		// [RH] Restore cvars demo might have changed

		delete[] demobuffer;
		demoplayback = false;
		netdemo = false;
		netgame = false;
		multiplayer = false;
		singletics = false;
		{
			int i;

			for (i = 1; i < MAXPLAYERS; i++)
				playeringame[i] = 0;
		}
		consoleplayer = 0;

		if (singledemo || timingdemo)
		{
			if (timingdemo)
			{
				// Trying to get back to a stable state after timing a demo
				// seems to cause problems. I don't feel like fixing that
				// right now.
				I_FatalError ("timed %i gametics in %i realtics (%.1f fps)\n"
							  "(This is not really an error.)", gametic,
							  endtime, (float)gametic/(float)endtime*(float)TICRATE);
			}
			else
			{
				Printf ("Demo ended.\n");
			}
			gameaction = ga_fullconsole;
			timingdemo = false;
			return false;
		}
		else
		{
			D_AdvanceDemo (); 
		}

		return true; 
	}

	if (demorecording)
	{
		byte *formlen;

		WriteByte (DEM_STOP, &demo_p);

		if (demo_compress)
		{
			// Now that the entire BODY chunk has been created, replace it with
			// a compressed version. If the BODY successfully compresses, the
			// contents of the COMP chunk will be changed to indicate the
			// uncompressed size of the BODY.
			uLong len = demo_p - demobodyspot;
			uLong outlen = (len + len/100 + 12);
			Byte *compressed = new Byte[outlen];
			int r = compress2 (compressed, &outlen, demobodyspot, len, 9);
			if (r == Z_OK && outlen < len)
			{
				formlen = democompspot;
				WriteLong (len, &democompspot);
				memcpy (demobodyspot, compressed, outlen);
				demo_p = demobodyspot + outlen;
			}
			delete[] compressed;
		}
		FinishChunk (&demo_p);
		formlen = demobuffer + 4;
		WriteLong (demo_p - demobuffer - 8, &formlen);

		M_WriteFile (demoname, demobuffer, demo_p - demobuffer); 
		free (demobuffer); 
		demorecording = false;
		stoprecording = false;
		Printf ("Demo %s recorded\n", demoname); 
	}

	return false; 
}
