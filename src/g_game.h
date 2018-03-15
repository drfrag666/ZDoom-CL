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
//	 Duh.
// 
//-----------------------------------------------------------------------------


#ifndef __G_GAME__
#define __G_GAME__

#include "doomdef.h"
#include "d_event.h"



//
// GAME
//
void G_DeathMatchSpawnPlayer (int playernum);

void G_DeferedPlayDemo (char* demo);

// Can be called by the startup code or M_Responder,
// calls P_SetupLevel or W_EnterWorld.
void G_LoadGame (const char* name);

void G_DoLoadGame (void);

// Called by M_Responder.
void G_SaveGame (const char *filename, const char *description);

// Only called by startup code.
void G_RecordDemo (char* name);

void G_BeginRecording (const char *startmap);

void G_PlayDemo (char* name);
void G_TimeDemo (char* name);
BOOL G_CheckDemoStatus (void);

void G_WorldDone (void);

void G_Ticker (void);
BOOL G_Responder (event_t*	ev);

void G_ScreenShot (char *filename);

FString G_BuildSaveName (const char *prefix, int slot);

struct PNGHandle;
bool G_CheckSaveGameWads (PNGHandle *png, bool printwarn);

enum EFinishLevelType
{
	FINISH_SameHub,
	FINISH_NextHub,
	FINISH_NoHub
};

void G_PlayerFinishLevel (int player, EFinishLevelType mode, bool resetinventory);

void G_DoReborn (int playernum, bool freshbot);

// Adds pitch to consoleplayer's viewpitch and clamps it
void G_AddViewPitch (int look);

// Adds to consoleplayer's viewangle if allowed
void G_AddViewAngle (int yaw);

#define BODYQUESIZE 	32
class AActor;
extern AActor *bodyque[BODYQUESIZE]; 
extern int bodyqueslot; 


#endif
