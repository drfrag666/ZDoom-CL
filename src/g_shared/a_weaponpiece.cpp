#include "a_pickups.h"
#include "a_weaponpiece.h"
#include "doomstat.h"



// an internal class to hold the information for player class independent weapon piece handling
class AWeaponHolder : public AInventory
{
	DECLARE_ACTOR(AWeaponHolder, AInventory)

public:
	int PieceMask;
	const PClass * PieceWeapon;

	void Serialize (FArchive &arc)
	{
		Super::Serialize(arc);
		arc << PieceMask << PieceWeapon;
	}
};



IMPLEMENT_STATELESS_ACTOR (AWeaponHolder, Any, -1, 0)
	PROP_Flags (MF_NOBLOCKMAP|MF_NOSECTOR)
	PROP_Inventory_FlagsSet (IF_UNDROPPABLE)
END_DEFAULTS

IMPLEMENT_STATELESS_ACTOR (AWeaponPiece, Any, -1, 0)
END_DEFAULTS


void AWeaponPiece::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << WeaponClass << FullWeapon << PieceValue;
}

//==========================================================================
//
// TryPickupWeaponPiece
//
//==========================================================================

bool AWeaponPiece::TryPickup (AActor *toucher)
{
	AInventory * inv;
	AWeaponHolder * hold=NULL;
	bool shouldStay = PrivateShouldStay ();
	int gaveAmmo;
	AWeapon * Defaults=(AWeapon*)GetDefaultByType(WeaponClass);

	FullWeapon=NULL;
	for(inv=toucher->Inventory;inv;inv=inv->Inventory)
	{
		if (inv->IsKindOf(RUNTIME_CLASS(AWeaponHolder)))
		{
			hold=static_cast<AWeaponHolder*>(inv);

			if (hold->PieceWeapon==WeaponClass) break;
			hold=NULL;
		}
	}
	if (!hold)
	{
		hold=static_cast<AWeaponHolder*>(Spawn(RUNTIME_CLASS(AWeaponHolder), 0, 0, 0, NO_REPLACE));
		hold->BecomeItem();
		hold->AttachToOwner(toucher);
		hold->PieceMask=0;
		hold->PieceWeapon=WeaponClass;
	}


	if (shouldStay)
	{ 
		// Cooperative net-game
		if (hold->PieceMask & PieceValue)
		{ 
			// Already has the piece
			return false;
		}
		toucher->GiveAmmo (Defaults->AmmoType1, Defaults->AmmoGive1);
		toucher->GiveAmmo (Defaults->AmmoType2, Defaults->AmmoGive2);
	}
	else
	{ // Deathmatch or singleplayer game
		gaveAmmo = toucher->GiveAmmo (Defaults->AmmoType1, Defaults->AmmoGive1) +
					toucher->GiveAmmo (Defaults->AmmoType2, Defaults->AmmoGive2);
		
		if (hold->PieceMask & PieceValue)
		{ 
			// Already has the piece, check if mana needed
			if (!gaveAmmo) return false;
			GiveQuest (toucher);
			GoAwayAndDie();
			return true;
		}
	}

	hold->PieceMask |= PieceValue;

	// Check if  weapon assembled
	if (hold->PieceMask== (1<<Defaults->health)-1)
	{
		if (!toucher->FindInventory (WeaponClass))
		{
			FullWeapon= static_cast<AWeapon*>(Spawn(WeaponClass, 0, 0, 0, NO_REPLACE));
			
			// The weapon itself should not give more ammo to the player!
			FullWeapon->AmmoGive1=0;
			FullWeapon->AmmoGive2=0;
			FullWeapon->AttachToOwner(toucher);
			FullWeapon->AmmoGive1=Defaults->AmmoGive1;
			FullWeapon->AmmoGive2=Defaults->AmmoGive2;
		}
	}
	GiveQuest (toucher);
	GoAwayAndDie();
	return true;
}

bool AWeaponPiece::ShouldStay ()
{
	return PrivateShouldStay ();
}

bool AWeaponPiece::PrivateShouldStay ()
{
	// We want a weapon piece to behave like a weapon, so follow the exact
	// same logic as weapons when deciding whether or not to stay.
	if (((multiplayer &&
		(!deathmatch && !alwaysapplydmflags)) || (dmflags & DF_WEAPONS_STAY)) &&
		!(flags&MF_DROPPED))
	{
		return true;
	}
	return false;
}

//===========================================================================
//
// PickupMessage
//
// Returns the message to print when this actor is picked up.
//
//===========================================================================

const char *AWeaponPiece::PickupMessage ()
{
	if (FullWeapon) 
	{
		return FullWeapon->PickupMessage();
	}
	else
	{
		return Super::PickupMessage();
	}
}

//===========================================================================
//
// DoPlayPickupSound
//
// Plays a sound when this actor is picked up.
//
//===========================================================================

void AWeaponPiece::PlayPickupSound (AActor *toucher)
{
	if (FullWeapon) FullWeapon->PlayPickupSound(toucher);
	else Super::PlayPickupSound(toucher);
}

