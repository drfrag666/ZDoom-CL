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
// DESCRIPTION:
//	 All the global variables that store the internal state.
//	 Theoretically speaking, the internal state of the engine
//	  should be found by looking at the variables collected
//	  here, and every relevant module will have to include
//	  this header file.
//	 In practice, things are a bit messy.
//
//-----------------------------------------------------------------------------


#ifndef __D_STATE__
#define __D_STATE__

// We need globally shared data structures,
//	for defining the global state variables.
// We need the player data structure as well.
//#include "d_player.h"

#include "doomdata.h"
#include "d_net.h"
#include "g_level.h"

// We also need the definition of a cvar
#include "c_cvars.h"

// -----------------------
// Game speed.
//
enum EGameSpeed
{
	SPEED_Normal,
	SPEED_Fast,
};
extern EGameSpeed GameSpeed;


// ------------------------
// Command line parameters.
//
extern	BOOL			devparm;		// DEBUG: launched with -devparm



// -----------------------------------------------------
// Game Mode - identify IWAD as shareware, retail etc.
//
extern GameMode_t		gamemode;
extern GameMission_t	gamemission;

// -------------------------------------------
// Selected skill type, map etc.
//

extern	char			startmap[8];		// [RH] Actual map name now

extern	BOOL 			autostart;

// Selected by user. 
EXTERN_CVAR (Int, gameskill);
extern	int				NextSkill;			// [RH] Skill to use at next level load

// Nightmare mode flag, single player.
extern	int 			respawnmonsters;

// Netgame? Only true if >1 player.
extern	BOOL			netgame;

// Bot game? Like netgame, but doesn't involve network communication.
extern	BOOL			multiplayer;

// Flag: true only if started as net deathmatch.
EXTERN_CVAR (Int, deathmatch)

// [RH] Pretend as deathmatch for purposes of dmflags
EXTERN_CVAR (Bool, alwaysapplydmflags)

// [RH] Teamplay mode
EXTERN_CVAR (Bool, teamplay)

// [RH] Friendly fire amount
EXTERN_CVAR (Float, teamdamage)

// [RH] The class the player will spawn as in single player,
// in case using a random class with Hexen.
extern int SinglePlayerClass[MAXPLAYERS];

// -------------------------
// Internal parameters for sound rendering.

EXTERN_CVAR (Float, snd_sfxvolume)		// maximum volume for sound
EXTERN_CVAR (Float, snd_musicvolume)	// maximum volume for music


// -------------------------
// Status flags for refresh.
//

enum EMenuState
{
	MENU_Off,			// Menu is closed
	MENU_On,			// Menu is opened
	MENU_WaitKey,		// Menu is opened and waiting for a key in the controls menu
	MENU_OnNoPause,		// Menu is opened but does not pause the game
};

extern	bool			automapactive;	// In AutoMap mode?
extern	EMenuState		menuactive; 	// Menu overlayed?
extern	int				paused; 		// Game Pause?


extern	bool			viewactive;

extern	BOOL	 		nodrawers;
extern	BOOL	 		noblit;

extern	int 			viewwindowx;
extern	int 			viewwindowy;
extern	"C" int 		viewheight;
extern	"C" int 		viewwidth;
extern	"C"	int			halfviewwidth;		// [RH] Half view width, for plane drawing
extern	"C" int			realviewwidth;		// [RH] Physical width of view window
extern	"C" int			realviewheight;		// [RH] Physical height of view window
extern	"C" int			detailxshift;		// [RH] X shift for horizontal detail level
extern	"C" int			detailyshift;		// [RH] Y shift for vertical detail level





// This one is related to the 3-screen display mode.
// ANG90 = left side, ANG270 = right
extern	int				viewangleoffset;

// Player taking events. i.e. The local player.
extern	int				consoleplayer;	


extern level_locals_t level;


// --------------------------------------
// DEMO playback/recording related stuff.
// No demo, there is a human player in charge?
// Disable save/end game?
extern	bool			usergame;

extern	bool			demoplayback;
extern	bool			demorecording;
extern	int				demover;

// Quit after playing a demo from cmdline.
extern	BOOL			singledemo; 	




extern	gamestate_t 	gamestate;

extern	int				SaveVersion;




//-----------------------------
// Internal parameters, fixed.
// These are set by the engine, and not changed
//	according to user inputs. Partly load from
//	WAD, partly set at startup time.



extern	int 			gametic;


// Alive? Disconnected?
extern	bool	 		playeringame[MAXPLAYERS];


// Player spawn spots for deathmatch.
extern TArray<mapthing2_t> deathmatchstarts;

// Player spawn spots.
extern	mapthing2_t		playerstarts[MAXPLAYERS];

// Intermission stats.
// Parameters for world map / intermission.
extern	struct wbstartstruct_s wminfo; 







//-----------------------------------------
// Internal parameters, used for engine.
//

// File handling stuff.
extern	FILE*			debugfile;

// if true, load all graphics at level load
extern	BOOL	 		precache;


//-------
//REFRESH
//-------

// wipegamestate can be set to -1
//	to force a wipe on the next draw
extern gamestate_t wipegamestate;
extern bool setsizeneeded;
extern BOOL setmodeneeded;

extern int BorderNeedRefresh;
extern int BorderTopRefresh;


EXTERN_CVAR (Float, mouse_sensitivity)
//?
// debug flag to cancel adaptiveness
extern	BOOL	 		singletics; 	

extern	int 			bodyqueslot;



// Needed to store the number of the dummy sky flat.
// Used for rendering,
//	as well as tracking projectiles etc.
extern int				skyflatnum;



// Netgame stuff (buffers and pointers, i.e. indices).

// This is the interface to the packet driver, a separate program
// in DOS, but just an abstraction here.
extern	doomcom_t		doomcom;

extern	struct ticcmd_t	localcmds[LOCALCMDTICS];

extern	int 			maketic;
extern	int 			nettics[MAXNETNODES];

extern	ticcmd_t		netcmds[MAXPLAYERS][BACKUPTICS];
extern	int 			ticdup;


// ---- [RH] ----
EXTERN_CVAR (Bool, developer)

extern bool ToggleFullscreen;

extern float JoyAxes[6];

extern int Net_Arbitrator;

EXTERN_CVAR (Bool, var_friction)
EXTERN_CVAR (Bool, var_pushers)


// [RH] Miscellaneous info for DeHackEd support
struct DehInfo
{
	int StartHealth;
	int StartBullets;
	int MaxHealth;
	int MaxArmor;
	int GreenAC;
	int BlueAC;
	int MaxSoulsphere;
	int SoulsphereHealth;
	int MegasphereHealth;
	int GodHealth;
	int FAArmor;
	int FAAC;
	int KFAArmor;
	int KFAAC;
	char PlayerSprite[5];
	BYTE ExplosionStyle;
	fixed_t ExplosionAlpha;
	int NoAutofreeze;
};
extern DehInfo deh;
EXTERN_CVAR (Int, infighting)

// [RH] Deathmatch flags

EXTERN_CVAR (Int, dmflags);
EXTERN_CVAR (Int, dmflags2);	// [BC]

EXTERN_CVAR (Int, compatflags);
extern int i_compatflags;

#endif
