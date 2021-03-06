/*
** p_things.cpp
** ACS-accessible thing utilities
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

#include "doomtype.h"
#include "p_local.h"
#include "p_effect.h"
#include "info.h"
#include "s_sound.h"
#include "tables.h"
#include "doomstat.h"
#include "m_random.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "a_sharedglobal.h"
#include "gi.h"
#include "templates.h"

// List of spawnable things for the Thing_Spawn and Thing_Projectile specials.
const PClass *SpawnableThings[MAX_SPAWNABLES];

static FRandom pr_leadtarget ("LeadTarget");

bool P_Thing_Spawn (int tid, int type, angle_t angle, bool fog, int newtid)
{
	int rtn = 0;
	const PClass *kind;
	AActor *spot, *mobj;
	FActorIterator iterator (tid);

	if (type >= MAX_SPAWNABLES)
		return false;

	if ( (kind = SpawnableThings[type]) == NULL)
		return false;

	// Handle decorate replacements.
	kind = kind->ActorInfo->GetReplacement()->Class;

	if ((GetDefaultByType (kind)->flags3 & MF3_ISMONSTER) && (dmflags & DF_NO_MONSTERS))
		return false;

	while ( (spot = iterator.Next ()) )
	{
		mobj = Spawn (kind, spot->x, spot->y, spot->z, ALLOW_REPLACE);

		if (mobj != NULL)
		{
			DWORD oldFlags2 = mobj->flags2;
			mobj->flags2 |= MF2_PASSMOBJ;
			if (P_TestMobjLocation (mobj))
			{
				rtn++;
				mobj->angle = (angle != ANGLE_MAX ? angle : spot->angle);
				if (fog)
				{
					Spawn<ATeleportFog> (spot->x, spot->y, spot->z + TELEFOGHEIGHT, ALLOW_REPLACE);
				}
				if (mobj->flags & MF_SPECIAL)
					mobj->flags |= MF_DROPPED;	// Don't respawn
				mobj->tid = newtid;
				mobj->AddToHash ();
				mobj->flags2 = oldFlags2;
			}
			else
			{
				// If this is a monster, subtract it from the total monster
				// count, because it already added to it during spawning.
				if (mobj->CountsAsKill())
				{
					level.total_monsters--;
				}
				// Same, for items
				if (mobj->flags & MF_COUNTITEM)
				{
					level.total_items--;
				}
				mobj->Destroy ();
				rtn = false;
			}
		}
	}

	return rtn != 0;
}

// [BC] Added
// [RH] Fixed

bool P_MoveThing(AActor * source, fixed_t x, fixed_t y, fixed_t z, bool fog)
{
	fixed_t oldx, oldy, oldz;

	oldx = source->x;
	oldy = source->y;
	oldz = source->z;

	source->SetOrigin (x, y, z);
	if (P_TestMobjLocation (source))
	{
		if (fog)
		{
			Spawn<ATeleportFog> (x, y, z + TELEFOGHEIGHT, ALLOW_REPLACE);
			Spawn<ATeleportFog> (oldx, oldy, oldz + TELEFOGHEIGHT, ALLOW_REPLACE);
		}
		return true;
	}
	else
	{
		source->SetOrigin (oldx, oldy, oldz);
		return false;
	}
}

bool P_Thing_Move (int tid, int mapspot, bool fog)
{
	FActorIterator iterator1 (tid);
	FActorIterator iterator2 (mapspot);
	AActor *source, *target;

	source = iterator1.Next ();
	target = iterator2.Next ();

	if (source != NULL && target != NULL)
	{
		return P_MoveThing(source, target->x, target->y, target->z, fog);
	}
	return false;
}

bool P_Thing_Projectile (int tid, int type, const char * type_name, angle_t angle,
	fixed_t speed, fixed_t vspeed, int dest, AActor *forcedest, int gravity, int newtid,
	bool leadTarget)
{
	int rtn = 0;
	const PClass *kind;
	AActor *spot, *mobj, *targ = forcedest;
	FActorIterator iterator (tid);
	float fspeed = float(speed);
	int defflags3;

	if (type_name == NULL)
	{
		if (type >= MAX_SPAWNABLES)
			return false;

		if ((kind = SpawnableThings[type]) == NULL)
			return false;
	}
	else
	{
		if ((kind = PClass::FindClass(type_name)) == NULL || kind->ActorInfo == NULL)
			return false;
	}


	// Handle decorate replacements.
	kind = kind->ActorInfo->GetReplacement()->Class;

	defflags3 = GetDefaultByType (kind)->flags3;
	if ((defflags3 & MF3_ISMONSTER) && (dmflags & DF_NO_MONSTERS))
		return false;

	while ( (spot = iterator.Next ()) )
	{
		FActorIterator tit (dest);

		if (dest == 0 || (targ = tit.Next()))
		{
			do
			{
				fixed_t z = spot->z;
				if (defflags3 & MF3_FLOORHUGGER)
				{
					z = ONFLOORZ;
				}
				else if (defflags3 & MF3_CEILINGHUGGER)
				{
					z = ONCEILINGZ;
				}
				else if (z != ONFLOORZ)
				{
					z -= spot->floorclip;
				}
				mobj = Spawn (kind, spot->x, spot->y, z, ALLOW_REPLACE);

				if (mobj)
				{
					mobj->tid = newtid;
					mobj->AddToHash ();
					if (mobj->SeeSound)
					{
						S_SoundID (mobj, CHAN_VOICE, mobj->SeeSound, 1, ATTN_NORM);
					}
					if (gravity)
					{
						mobj->flags &= ~MF_NOGRAVITY;
						if (!(mobj->flags3 & MF3_ISMONSTER) && gravity == 1)
						{
							mobj->flags2 |= MF2_LOGRAV;
						}
					}
					else
					{
						mobj->flags |= MF_NOGRAVITY;
					}
					mobj->target = spot;

					if (targ != NULL)
					{
						fixed_t spot[3] = { targ->x, targ->y, targ->z+targ->height/2 };
						vec3_t aim =
						{
							float(spot[0] - mobj->x),
							float(spot[1] - mobj->y),
							float(spot[2] - mobj->z)
						};

						if (leadTarget && speed > 0 && (targ->momx | targ->momy | targ->momz))
						{
							// Aiming at the target's position some time in the future
							// is basically just an application of the law of sines:
							//     a/sin(A) = b/sin(B)
							// Thanks to all those on the notgod phorum for helping me
							// with the math. I don't think I would have thought of using
							// trig alone had I been left to solve it by myself.

							double tvel[3] = { double(targ->momx), double(targ->momy), double(targ->momz) };
							if (!(targ->flags & MF_NOGRAVITY) && targ->waterlevel < 3)
							{ // If the target is subject to gravity and not underwater,
							  // assume that it isn't moving vertically. Thanks to gravity,
							  // even if we did consider the vertical component of the target's
							  // velocity, we would still miss more often than not.
								tvel[2] = 0.0;
								if ((targ->momx | targ->momy) == 0)
								{
									goto nolead;
								}
							}
							double dist = sqrt (aim[0]*aim[0] + aim[1]*aim[1] + aim[2]*aim[2]);
							double targspeed = sqrt (tvel[0]*tvel[0] + tvel[1]*tvel[1] + tvel[2]*tvel[2]);
							double ydotx = -aim[0]*tvel[0] - aim[1]*tvel[1] - aim[2]*tvel[2];
							double a = acos (clamp (ydotx / targspeed / dist, -1.0, 1.0));
							double multiplier = double(pr_leadtarget.Random2())*0.1/255+1.1;
							double sinb = clamp (targspeed*multiplier * sin(a) / fspeed, -1.0, 1.0);
							double cosb = cos (asin (sinb));

							// Use the cross product of two of the triangle's sides to get a
							// rotation vector.
							double rv[3] =
							{
								tvel[1]*aim[2] - tvel[2]*aim[1],
								tvel[2]*aim[0] - tvel[0]*aim[2],
								tvel[0]*aim[1] - tvel[1]*aim[0]
							};
							// The vector must be normalized.
							double irvlen = 1.0 / sqrt(rv[0]*rv[0] + rv[1]*rv[1] + rv[2]*rv[2]);
							rv[0] *= irvlen;
							rv[1] *= irvlen;
							rv[2] *= irvlen;
							// Now combine the rotation vector with angle b to get a rotation matrix.
							double t = 1.0 - cosb;
							double rm[3][3] =
							{
								{t*rv[0]*rv[0]+cosb, t*rv[0]*rv[1]-sinb*rv[2], t*rv[0]*rv[2]+sinb*rv[1]},
								{t*rv[0]*rv[1]+sinb*rv[2], t*rv[1]*rv[1]+cosb, t*rv[1]*rv[2]-sinb*rv[0]},
								{t*rv[0]*rv[2]-sinb*rv[1], t*rv[1]*rv[2]+sinb*rv[0], t*rv[2]*rv[2]+cosb}
							};
							// And multiply the original aim vector with the matrix to get a
							// new aim vector that leads the target.
							double aimvec[3] =
							{
								rm[0][0]*aim[0] + rm[1][0]*aim[1] + rm[2][0]*aim[2],
								rm[0][1]*aim[0] + rm[1][1]*aim[1] + rm[2][1]*aim[2],
								rm[0][2]*aim[0] + rm[1][2]*aim[1] + rm[2][2]*aim[2]
							};
							// And make the projectile follow that vector at the desired speed.
							double aimscale = fspeed / dist;
							mobj->momx = fixed_t (aimvec[0] * aimscale);
							mobj->momy = fixed_t (aimvec[1] * aimscale);
							mobj->momz = fixed_t (aimvec[2] * aimscale);
							mobj->angle = R_PointToAngle2 (0, 0, mobj->momx, mobj->momy);
						}
						else
						{
nolead:
							mobj->angle = R_PointToAngle2 (mobj->x, mobj->y, targ->x, targ->y);
							VectorNormalize (aim);
							mobj->momx = fixed_t(aim[0] * fspeed);
							mobj->momy = fixed_t(aim[1] * fspeed);
							mobj->momz = fixed_t(aim[2] * fspeed);
						}
						if (mobj->flags2 & MF2_SEEKERMISSILE)
						{
							mobj->tracer = targ;
						}
					}
					else
					{
						mobj->angle = angle;
						mobj->momx = FixedMul (speed, finecosine[angle>>ANGLETOFINESHIFT]);
						mobj->momy = FixedMul (speed, finesine[angle>>ANGLETOFINESHIFT]);
						mobj->momz = vspeed;
					}
					// Set the missile's speed to reflect the speed it was spawned at.
					if (mobj->flags & MF_MISSILE)
					{
						mobj->Speed = fixed_t (sqrtf (float(speed*speed + vspeed*vspeed)));
					}
					// Hugger missiles don't have any vertical velocity
					if (mobj->flags3 & (MF3_FLOORHUGGER|MF3_CEILINGHUGGER))
					{
						mobj->momz = 0;
					}
					if (mobj->flags & MF_SPECIAL)
					{
						mobj->flags |= MF_DROPPED;
					}
					if (mobj->flags & MF_MISSILE)
					{
						if (P_CheckMissileSpawn (mobj))
						{
							rtn = true;
						}
					}
					else if (!P_TestMobjLocation (mobj))
					{
						// If this is a monster, subtract it from the total monster
						// count, because it already added to it during spawning.
						if (mobj->CountsAsKill())
						{
							level.total_monsters--;
						}
						// Same, for items
						if (mobj->flags & MF_COUNTITEM)
						{
							level.total_items--;
						}
						mobj->Destroy ();
					}
					else
					{
						// It spawned fine.
						rtn = 1;
					}
				}
			} while (dest != 0 && (targ = tit.Next()));
		} 
	}

	return rtn != 0;
}

CCMD (dumpspawnables)
{
	int i;

	for (i = 0; i < MAX_SPAWNABLES; i++)
	{
		if (SpawnableThings[i] != NULL)
		{
			Printf ("%d %s\n", i, SpawnableThings[i]->TypeName.GetChars());
		}
	}
}
