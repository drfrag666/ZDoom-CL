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
// DESCRIPTION:
//		Cheat sequence checking.
//
//-----------------------------------------------------------------------------


#include <stdlib.h>
#include <math.h>

#include "m_cheat.h"
#include "d_player.h"
#include "doomstat.h"
#include "gstrings.h"
#include "p_local.h"
#include "a_doomglobal.h"
#include "a_strifeglobal.h"
#include "gi.h"
#include "p_enemy.h"
#include "sbar.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "w_wad.h"
#include "a_keys.h"
#include "templates.h"
#include "p_lnspec.h"

// [RH] Actually handle the cheat. The cheat code in st_stuff.c now just
// writes some bytes to the network data stream, and the network code
// later calls us.

void cht_DoCheat (player_t *player, int cheat)
{
	static const PClass *BeholdPowers[9] =
	{
		RUNTIME_CLASS(APowerInvulnerable),
		RUNTIME_CLASS(APowerStrength),
		RUNTIME_CLASS(APowerInvisibility),
		RUNTIME_CLASS(APowerIronFeet),
		NULL, // MapRevealer
		RUNTIME_CLASS(APowerLightAmp),
		RUNTIME_CLASS(APowerShadow),
		RUNTIME_CLASS(APowerMask),
		RUNTIME_CLASS(APowerTargeter)
	};
	const PClass *type;
	AInventory *item;
	const char *msg = "";
	char msgbuild[32];
	int i;

	switch (cheat)
	{
	case CHT_IDDQD:
		if (!(player->cheats & CF_GODMODE) && player->playerstate == PST_LIVE)
		{
			if (player->mo)
				player->mo->health = deh.GodHealth;

			player->health = deh.GodHealth;
		}
		// fall through to CHT_GOD
	case CHT_GOD:
		player->cheats ^= CF_GODMODE;
		if (player->cheats & CF_GODMODE)
			msg = GStrings("STSTR_DQDON");
		else
			msg = GStrings("STSTR_DQDOFF");
		SB_state = screen->GetPageCount ();
		break;

	case CHT_BUDDHA:
		player->cheats ^= CF_BUDDHA;
		if (player->cheats & CF_BUDDHA)
			msg = GStrings("TXT_BUDDHAON");
		else
			msg = GStrings("TXT_BUDDHAOFF");
		break;

	case CHT_NOCLIP:
		player->cheats ^= CF_NOCLIP;
		if (player->cheats & CF_NOCLIP)
			msg = GStrings("STSTR_NCON");
		else
			msg = GStrings("STSTR_NCOFF");
		break;

	case CHT_NOMOMENTUM:
		player->cheats ^= CF_NOMOMENTUM;
		if (player->cheats & CF_NOMOMENTUM)
			msg = "LEAD BOOTS ON";
		else
			msg = "LEAD BOOTS OFF";
		break;

	case CHT_FLY:
		if (player->mo != NULL)
		{
			player->cheats ^= CF_FLY;
			if (player->cheats & CF_FLY)
			{
				player->mo->flags |= MF_NOGRAVITY;
				player->mo->flags2 |= MF2_FLY;
				msg = "You feel lighter";
			}
			else
			{
				player->mo->flags &= ~MF_NOGRAVITY;
				player->mo->flags2 &= ~MF2_FLY;
				msg = "Gravity weighs you down";
			}
		}
		break;

	case CHT_MORPH:
		if (player->morphTics)
		{
			if (P_UndoPlayerMorph (player))
			{
				msg = "You feel like yourself again";
			}
		}
		else if (P_MorphPlayer (player,
			PClass::FindClass (gameinfo.gametype == GAME_Heretic ? NAME_ChickenPlayer : NAME_PigPlayer)))
		{
			msg = "You feel strange...";
		}
		break;

	case CHT_NOTARGET:
		player->cheats ^= CF_NOTARGET;
		if (player->cheats & CF_NOTARGET)
			msg = "notarget ON";
		else
			msg = "notarget OFF";
		break;

	case CHT_ANUBIS:
		player->cheats ^= CF_FRIGHTENING;
		if (player->cheats & CF_FRIGHTENING)
			msg = "\"Quake with fear!\"";
		else
			msg = "No more ogre armor";
		break;

	case CHT_CHASECAM:
		player->cheats ^= CF_CHASECAM;
		if (player->cheats & CF_CHASECAM)
			msg = "chasecam ON";
		else
			msg = "chasecam OFF";
		R_ResetViewInterpolation ();
		break;

	case CHT_CHAINSAW:
		if (player->mo != NULL && player->health >= 0)
		{
			type = PClass::FindClass ("Chainsaw");
			if (player->mo->FindInventory (type) == NULL)
			{
				player->mo->GiveInventoryType (type);
			}
			msg = GStrings("STSTR_CHOPPERS");
		}
		// [RH] The original cheat also set powers[pw_invulnerability] to true.
		// Since this is a timer and not a boolean, it effectively turned off
		// the invulnerability powerup, although it looks like it was meant to
		// turn it on.
		break;

	case CHT_POWER:
		if (player->mo != NULL && player->health >= 0)
		{
			item = player->mo->FindInventory (RUNTIME_CLASS(APowerWeaponLevel2));
			if (item != NULL)
			{
				item->Destroy ();
				msg = GStrings("TXT_CHEATPOWEROFF");
			}
			else
			{
				player->mo->GiveInventoryType (RUNTIME_CLASS(APowerWeaponLevel2));
				msg = GStrings("TXT_CHEATPOWERON");
			}
		}
		break;

	case CHT_IDKFA:
		cht_Give (player, "backpack");
		cht_Give (player, "weapons");
		cht_Give (player, "ammo");
		cht_Give (player, "keys");
		cht_Give (player, "armor");
		msg = GStrings("STSTR_KFAADDED");
		break;

	case CHT_IDFA:
		cht_Give (player, "backpack");
		cht_Give (player, "weapons");
		cht_Give (player, "ammo");
		cht_Give (player, "armor");
		msg = GStrings("STSTR_FAADDED");
		break;

	case CHT_BEHOLDV:
	case CHT_BEHOLDS:
	case CHT_BEHOLDI:
	case CHT_BEHOLDR:
	case CHT_BEHOLDA:
	case CHT_BEHOLDL:
	case CHT_PUMPUPI:
	case CHT_PUMPUPM:
	case CHT_PUMPUPT:
		i = cheat - CHT_BEHOLDV;

		if (i == 4)
		{
			level.flags ^= LEVEL_ALLMAP;
		}
		else if (player->mo != NULL && player->health >= 0)
		{
			item = player->mo->FindInventory (BeholdPowers[i]);
			if (item == NULL)
			{
				player->mo->GiveInventoryType (BeholdPowers[i]);
				if (cheat == CHT_BEHOLDS)
				{
					P_GiveBody (player->mo, -100);
				}
			}
			else
			{
				item->Destroy ();
			}
		}
		msg = GStrings("STSTR_BEHOLDX");
		break;

	case CHT_MASSACRE:
		{
			int killcount = P_Massacre ();
			// killough 3/22/98: make more intelligent about plural
			// Ty 03/27/98 - string(s) *not* externalized
			sprintf (msgbuild, "%d Monster%s Killed", killcount, killcount==1 ? "" : "s");
			msg = msgbuild;
		}
		break;

	case CHT_HEALTH:
		if (player->mo != NULL && player->playerstate == PST_LIVE)
		{
			player->health = player->mo->health = player->mo->GetDefault()->health;
			msg = GStrings("TXT_CHEATHEALTH");
		}
		break;

	case CHT_KEYS:
		cht_Give (player, "keys");
		msg = GStrings("TXT_CHEATKEYS");
		break;

	// [GRB]
	case CHT_RESSURECT:
		if (player->playerstate != PST_LIVE && player->mo != NULL)
		{
			if (player->mo->IsKindOf(RUNTIME_CLASS(APlayerChunk)))
			{
				Printf("Unable to resurrect. Player is no longer connected to its body.\n");
			}
			else
			{
				player->playerstate = PST_LIVE;
				player->health = player->mo->health = player->mo->GetDefault()->health;
				player->viewheight = ((APlayerPawn *)player->mo->GetDefault())->ViewHeight;
				player->mo->flags = player->mo->GetDefault()->flags;
				player->mo->flags2 = player->mo->GetDefault()->flags2;
				player->mo->flags3 = player->mo->GetDefault()->flags3;
				player->mo->flags4 = player->mo->GetDefault()->flags4;
				player->mo->flags5 = player->mo->GetDefault()->flags5;
				player->mo->renderflags &= ~RF_INVISIBLE;
				player->mo->height = player->mo->GetDefault()->height;
				player->mo->radius = player->mo->GetDefault()->radius;
 				player->mo->special1 = 0;	// required for the Hexen fighter's fist attack. 
 											// This gets set by AActor::Die as flag for the wimpy death and must be reset here.
				player->mo->SetState (player->mo->SpawnState);
				player->mo->Translation = TRANSLATION(TRANSLATION_Players, BYTE(player-players));
				player->mo->DamageType = MOD_UNKNOWN;
//				player->mo->GiveDefaultInventory();
				if (player->ReadyWeapon != NULL)
				{
					P_SetPsprite(player, ps_weapon, player->ReadyWeapon->UpState);
				}

				if (player->morphTics > 0)
				{
					P_UndoPlayerMorph(player);
				}

			}
		}
		break;

	case CHT_TAKEWEAPS:
		if (player->morphTics || player->mo != NULL)
		{
			return;
		}
		// Take away all weapons that are either non-wimpy or use ammo.
		for (item = player->mo->Inventory; item != NULL; )
		{
			AInventory *next = item->Inventory;
			if (item->IsKindOf (RUNTIME_CLASS(AWeapon)))
			{
				AWeapon *weap = static_cast<AWeapon *> (item);
				if (!(weap->WeaponFlags & WIF_WIMPY_WEAPON) ||
					weap->AmmoType1 != NULL)
				{
					item->Destroy ();
				}
			}
			item = next;
		}
		msg = GStrings("TXT_CHEATIDKFA");
		break;

	case CHT_NOWUDIE:
		cht_Suicide (player);
		msg = GStrings("TXT_CHEATIDDQD");
		break;

	case CHT_ALLARTI:
		for (int i=0;i<25;i++)
		{
			cht_Give (player, "artifacts");
		}
		msg = GStrings("TXT_CHEATARTIFACTS3");
		break;

	case CHT_PUZZLE:
		cht_Give (player, "puzzlepieces");
		msg = GStrings("TXT_CHEATARTIFACTS3");
		break;

	case CHT_MDK:
		if (player->mo == NULL)
		{
			Printf ("What do you want to kill outside of a game?\n");
		}
		else if (!deathmatch)
		{
			// Don't allow this in deathmatch even with cheats enabled, because it's
			// a very very cheap kill.
			P_LineAttack (player->mo, player->mo->angle, PLAYERMISSILERANGE,
				P_AimLineAttack (player->mo, player->mo->angle, PLAYERMISSILERANGE), 1000000,
				MOD_UNKNOWN, RUNTIME_CLASS(ABulletPuff));
		}
		break;

	case CHT_DONNYTRUMP:
		cht_Give (player, "HealthTraining");
		msg = "YOU GOT THE MIDAS TOUCH, BABY";
		break;

	case CHT_LEGO:
		if (player->mo != NULL && player->health >= 0)
		{
			int oldpieces = ASigil::GiveSigilPiece (player->mo);
			item = player->mo->FindInventory (RUNTIME_CLASS(ASigil));

			if (item != NULL)
			{
				if (oldpieces == 5)
				{
					item->Destroy ();
				}
				else
				{
					player->PendingWeapon = static_cast<AWeapon *> (item);
				}
			}
		}
		break;

	case CHT_PUMPUPH:
		cht_Give (player, "MedPatch");
		cht_Give (player, "MedicalKit");
		cht_Give (player, "SurgeryKit");
		msg = "you got the stuff!";
		break;

	case CHT_PUMPUPP:
		cht_Give (player, "AmmoSatchel");
		msg = "you got the stuff!";
		break;

	case CHT_PUMPUPS:
		cht_Give (player, "UpgradeStamina", 10);
		cht_Give (player, "UpgradeAccuracy");
		msg = "you got the stuff!";
		break;
	}

	if (!*msg)              // [SO] Don't print blank lines!
		return;

	if (player == &players[consoleplayer])
		Printf ("%s\n", msg);
	else
		Printf ("%s is a cheater: %s\n", player->userinfo.netname, msg);
}

void GiveSpawner (player_t *player, const PClass *type, int amount)
{
	if (player->mo == NULL || player->health <= 0)
	{
		return;
	}

	AInventory *item = static_cast<AInventory *>
		(Spawn (type, player->mo->x, player->mo->y, player->mo->z, NO_REPLACE));
	if (item != NULL)
	{
		if (amount > 0)
		{
			if (type->IsDescendantOf (RUNTIME_CLASS(ABasicArmorPickup)))
			{
				if (static_cast<ABasicArmorPickup*>(item)->SaveAmount != 0)
				{
					static_cast<ABasicArmorPickup*>(item)->SaveAmount *= amount;
				}
				else
				{
					static_cast<ABasicArmorPickup*>(item)->SaveAmount *= amount;
				}
			}
			else if (type->IsDescendantOf (RUNTIME_CLASS(ABasicArmorBonus)))
			{
				static_cast<ABasicArmorBonus*>(item)->SaveAmount *= amount;
			}
			else
			{
				item->Amount = MIN (amount, item->MaxAmount);
			}
		}
		if (!item->TryPickup (player->mo))
		{
			item->Destroy ();
		}
	}
}

void cht_Give (player_t *player, char *name, int amount)
{
	BOOL giveall;
	int i;
	const PClass *type;

	if (player != &players[consoleplayer])
		Printf ("%s is a cheater: give %s\n", player->userinfo.netname, name);

	if (player->mo == NULL || player->health <= 0)
	{
		return;
	}

	giveall = (stricmp (name, "all") == 0);

	if (giveall || stricmp (name, "health") == 0)
	{
		if (amount > 0)
		{
			if (player->mo)
			{
				player->mo->health += amount;
	  			player->health = player->mo->health;
			}
			else
			{
				player->health += amount;
			}
		}
		else
		{
			if (player->mo)
				player->mo->health = deh.GodHealth;
	  
			player->health = deh.GodHealth;
		}

		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "backpack") == 0)
	{
		// Select the correct type of backpack based on the game
		if (gameinfo.gametype == GAME_Heretic)
		{
			type = PClass::FindClass ("BagOfHolding");
		}
		else if (gameinfo.gametype == GAME_Strife)
		{
			type = PClass::FindClass ("AmmoSatchel");
		}
		else if (gameinfo.gametype == GAME_Doom)
		{
			type = PClass::FindClass ("Backpack");
		}
		else
		{ // Hexen doesn't have a backpack, foo!
			type = NULL;
		}
		if (type != NULL)
		{
			GiveSpawner (player, type, 1);
		}

		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "ammo") == 0)
	{
		// Find every unique type of ammo. Give it to the player if
		// he doesn't have it already, and set each to its maximum.
		for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
		{
			const PClass *type = PClass::m_Types[i];

			if (type->ParentClass == RUNTIME_CLASS(AAmmo))
			{
				AInventory *ammo = player->mo->FindInventory (type);
				if (ammo == NULL)
				{
					ammo = static_cast<AInventory *>(Spawn (type, 0, 0, 0, NO_REPLACE));
					ammo->AttachToOwner (player->mo);
					ammo->Amount = ammo->MaxAmount;
				}
				else if (ammo->Amount < ammo->MaxAmount)
				{
					ammo->Amount = ammo->MaxAmount;
				}
			}
		}

		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "armor") == 0)
	{
		if (gameinfo.gametype != GAME_Hexen)
		{
			ABasicArmorPickup *armor = Spawn<ABasicArmorPickup> (0,0,0, NO_REPLACE);
			armor->SaveAmount = 100*deh.BlueAC;
			armor->SavePercent = gameinfo.gametype != GAME_Heretic ? FRACUNIT/2 : FRACUNIT*3/4;
			if (!armor->TryPickup (player->mo))
			{
				armor->Destroy ();
			}
		}
		else
		{
			for (i = 0; i < 4; ++i)
			{
				AHexenArmor *armor = Spawn<AHexenArmor> (0,0,0, NO_REPLACE);
				armor->health = i;
				armor->Amount = 0;
				if (!armor->TryPickup (player->mo))
				{
					armor->Destroy ();
				}
			}
		}

		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "keys") == 0)
	{
		for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
		{
			if (PClass::m_Types[i]->IsDescendantOf (RUNTIME_CLASS(AKey)))
			{
				AKey *key = (AKey *)GetDefaultByType (PClass::m_Types[i]);
				if (key->KeyNumber != 0)
				{
					key = static_cast<AKey *>(Spawn (PClass::m_Types[i], 0,0,0, NO_REPLACE));
					if (!key->TryPickup (player->mo))
					{
						key->Destroy ();
					}
				}
			}
		}
		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "weapons") == 0)
	{
		AWeapon *savedpending = player->PendingWeapon;
		for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
		{
			type = PClass::m_Types[i];
			if (type != RUNTIME_CLASS(AWeapon) &&
				type->IsDescendantOf (RUNTIME_CLASS(AWeapon)))
			{
				AWeapon *def = (AWeapon*)GetDefaultByType (type);
				if (!(def->WeaponFlags & WIF_CHEATNOTWEAPON))
				{
					GiveSpawner (player, type, 1);
				}
			}
		}
		player->PendingWeapon = savedpending;

		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "artifacts") == 0)
	{
		for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
		{
			type = PClass::m_Types[i];
			if (type->IsDescendantOf (RUNTIME_CLASS(AInventory)))
			{
				AInventory *def = (AInventory*)GetDefaultByType (type);
				if (def->Icon > 0 && def->MaxAmount > 1 &&
					!type->IsDescendantOf (RUNTIME_CLASS(APuzzleItem)) &&
					!type->IsDescendantOf (RUNTIME_CLASS(APowerup)) &&
					!type->IsDescendantOf (RUNTIME_CLASS(AArmor)))
				{
					GiveSpawner (player, type, 1);
				}
			}
		}
		if (!giveall)
			return;
	}

	if (giveall || stricmp (name, "puzzlepieces") == 0)
	{
		for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
		{
			type = PClass::m_Types[i];
			if (type->IsDescendantOf (RUNTIME_CLASS(APuzzleItem)))
			{
				AInventory *def = (AInventory*)GetDefaultByType (type);
				if (def->Icon > 0)
				{
					GiveSpawner (player, type, 1);
				}
			}
		}
		if (!giveall)
			return;
	}

	if (giveall)
		return;

	type = PClass::FindClass (name);
	if (type == NULL || !type->IsDescendantOf (RUNTIME_CLASS(AInventory)))
	{
		if (player == &players[consoleplayer])
			Printf ("Unknown item \"%s\"\n", name);
	}
	else
	{
		GiveSpawner (player, type, amount);
	}
	return;
}

void cht_Suicide (player_t *plyr)
{
	if (plyr->mo != NULL)
	{
		plyr->mo->flags |= MF_SHOOTABLE;
		plyr->mo->flags2 &= ~MF2_INVULNERABLE;
		P_DamageMobj (plyr->mo, plyr->mo, plyr->mo, 1000000, MOD_SUICIDE);
		if (plyr->mo->health <= 0) plyr->mo->flags &= ~MF_SHOOTABLE;
	}
}

BOOL CheckCheatmode ();

CCMD (mdk)
{
	if (CheckCheatmode ())
		return;

	Net_WriteByte (DEM_GENERICCHEAT);
	Net_WriteByte (CHT_MDK);
}
