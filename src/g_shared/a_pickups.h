#ifndef __A_PICKUPS_H__
#define __A_PICKUPS_H__

#include "dobject.h"
#include "actor.h"
#include "info.h"

#define MAX_MANA				200

#define MAX_WEAPONS_PER_SLOT	8
#define NUM_WEAPON_SLOTS		10

class player_s;
class FConfigFile;
class AWeapon;

class FWeaponSlot
{
public:
	FWeaponSlot ();
	void Clear ();
	bool AddWeapon (const char *type);
	bool AddWeapon (const PClass *type);
	AWeapon *PickWeapon (player_s *player);
	int CountWeapons ();

	inline const PClass *GetWeapon (int index) const
	{
		return Weapons[index];
	}

	friend AWeapon *PickNextWeapon (player_s *player);
	friend AWeapon *PickPrevWeapon (player_s *player);

	friend struct FWeaponSlots;

private:
	const PClass *Weapons[MAX_WEAPONS_PER_SLOT];
};

AWeapon *PickNextWeapon (player_s *player);
AWeapon *PickPrevWeapon (player_s *player);

// FWeaponSlots::AddDefaultWeapon return codes
enum ESlotDef
{
	SLOTDEF_Exists,		// Weapon was already assigned a slot
	SLOTDEF_Added,		// Weapon was successfully added
	SLOTDEF_Full		// The specifed slot was full
};

struct FWeaponSlots
{
	FWeaponSlot Slots[NUM_WEAPON_SLOTS];

	void Clear ();
	bool LocateWeapon (const PClass *type, int *const slot, int *const index);
	ESlotDef AddDefaultWeapon (int slot, const PClass *type);
	int RestoreSlots (FConfigFile &config);
	void SaveSlots (FConfigFile &config);
};

extern FWeaponSlots LocalWeapons;

/************************************************************************/
/* Class definitions													*/
/************************************************************************/

// A pickup is anything the player can pickup (i.e. weapons, ammo, powerups, etc)

enum
{
	AIMETA_BASE = 0x71000,
	AIMETA_PickupMessage,		// string
	AIMETA_GiveQuest,			// optionally give one of the quest items.
	AIMETA_DropAmount,			// specifies the amount for a dropped ammo item
	AIMETA_LowHealth,
	AIMETA_LowHealthMessage,
};

enum
{
	IF_ACTIVATABLE		= 1<<0,		// can be activated
	IF_ACTIVATED		= 1<<1,		// is currently activated
	IF_PICKUPGOOD		= 1<<2,		// HandlePickup wants normal pickup FX to happen
	IF_QUIET			= 1<<3,		// Don't give feedback when picking up
	IF_AUTOACTIVATE		= 1<<4,		// Automatically activate item on pickup
	IF_UNDROPPABLE		= 1<<5,		// The player cannot manually drop the item
	IF_INVBAR			= 1<<6,		// Item appears in the inventory bar
	IF_HUBPOWER			= 1<<7,		// Powerup is kept when moving in a hub
	IF_INTERHUBSTRIP	= 1<<8,		// Item is removed when travelling between hubs
	IF_PICKUPFLASH		= 1<<9,		// Item "flashes" when picked up
	IF_ALWAYSPICKUP		= 1<<10,	// For IF_AUTOACTIVATE, MaxAmount=0 items: Always "pick up", even if it doesn't do anything
	IF_FANCYPICKUPSOUND	= 1<<11,	// Play pickup sound in "surround" mode
	IF_BIGPOWERUP		= 1<<12,	// Affected by RESPAWN_SUPER dmflag
};

struct vissprite_t;

class AInventory : public AActor
{
	DECLARE_ACTOR (AInventory, AActor)
	HAS_OBJECT_POINTERS
public:
	virtual void Touch (AActor *toucher);
	virtual void Serialize (FArchive &arc);

	virtual void BeginPlay ();
	virtual void Destroy ();
	virtual void Tick ();
	virtual bool ShouldRespawn ();
	virtual bool ShouldStay ();
	virtual void Hide ();
	virtual bool DoRespawn ();
	virtual bool TryPickup (AActor *toucher);
	virtual void DoPickupSpecial (AActor *toucher);
	virtual bool SpecialDropAction (AActor *dropper);
	virtual bool DrawPowerup (int x, int y);

	virtual const char *PickupMessage ();
	virtual void PlayPickupSound (AActor *toucher);

	AInventory *PrevItem () const;	// Returns the item preceding this one in the list.
	AInventory *PrevInv () const;	// Returns the previous item with IF_INVBAR set.
	AInventory *NextInv () const;	// Returns the next item with IF_INVBAR set.

	AActor *Owner;				// Who owns this item? NULL if it's still a pickup.
	int Amount;					// Amount of item this instance has
	int MaxAmount;				// Max amount of item this instance can have
	int RespawnTics;			// Tics from pickup time to respawn time
	int Icon;					// Icon to show on status bar or HUD
	int DropTime;				// Countdown after dropping

	DWORD ItemFlags;

	WORD PickupSound;

	virtual void BecomeItem ();
	virtual void BecomePickup ();
	virtual void AttachToOwner (AActor *other);
	virtual void DetachFromOwner ();
	virtual AInventory *CreateCopy (AActor *other);
	virtual AInventory *CreateTossable ();
	virtual bool GoAway ();
	virtual void GoAwayAndDie ();
	virtual bool HandlePickup (AInventory *item);
	virtual bool Use (bool pickup);
	virtual void Travelled ();
	virtual void OwnerDied ();

	virtual void AbsorbDamage (int damage, int damageType, int &newdamage);
	virtual void AlterWeaponSprite (vissprite_t *vis);

	virtual PalEntry GetBlend ();

protected:
	void GiveQuest(AActor * toucher);

private:
	static int StaticLastMessageTic;
	static const char *StaticLastMessage;
};

// CustomInventory: Supports the Use, Pickup, and Drop states from 96x
class ACustomInventory : public AInventory
{
	DECLARE_STATELESS_ACTOR (ACustomInventory, AInventory)
public:
	FState *UseState, *PickupState, *DropState;

	// This is used when an inventory item's use state sequence is executed.
	static bool CallStateChain (AActor *actor, FState *state);

	void Serialize (FArchive &arc);
	bool TryPickup (AActor *toucher);
	bool Use (bool pickup);
	bool SpecialDropAction (AActor *dropper);
};

// Ammo: Something a weapon needs to operate
class AAmmo : public AInventory
{
	DECLARE_STATELESS_ACTOR (AAmmo, AInventory)
public:
	void Serialize (FArchive &arc);
	AInventory *CreateCopy (AActor *other);
	bool HandlePickup (AInventory *item);
	const PClass *GetParentAmmo () const;

	int BackpackAmount, BackpackMaxAmount;
};

// A weapon is just that.
class AWeapon : public AInventory
{
	DECLARE_ACTOR (AWeapon, AInventory)
	HAS_OBJECT_POINTERS
public:
	DWORD WeaponFlags;
	const PClass *AmmoType1, *AmmoType2;	// Types of ammo used by this weapon
	int AmmoGive1, AmmoGive2;				// Amount of each ammo to get when picking up weapon
	int MinAmmo1, MinAmmo2;					// Minimum ammo needed to switch to this weapon
	int AmmoUse1, AmmoUse2;					// How much ammo to use with each shot
	int Kickback;
	fixed_t YAdjust;						// For viewing the weapon fullscreen
	WORD UpSound, ReadySound;				// Sounds when coming up and idle
	const PClass *SisterWeaponType;		// Another weapon to pick up with this one
	const PClass *ProjectileType;			// Projectile used by primary attack
	const PClass *AltProjectileType;		// Projectile used by alternate attack
	int SelectionOrder;						// Lower-numbered weapons get picked first
	fixed_t MoveCombatDist;					// Used by bots, but do they *really* need it?

	FState *UpState;
	FState *DownState;
	FState *ReadyState;
	FState *AtkState, *HoldAtkState;
	FState *AltAtkState, *AltHoldAtkState;
	FState *FlashState, *AltFlashState;

	// In-inventory instance variables
	AAmmo *Ammo1, *Ammo2;
	AWeapon *SisterWeapon;

	bool bAltFire;	// Set when this weapon's alternate fire is used.

	virtual void Serialize (FArchive &arc);
	virtual bool ShouldStay ();
	virtual void AttachToOwner (AActor *other);
	virtual bool HandlePickup (AInventory *item);
	virtual AInventory *CreateCopy (AActor *other);
	virtual AInventory *CreateTossable ();
	virtual bool TryPickup (AActor *toucher);
	virtual bool PickupForAmmo (AWeapon *ownedWeapon);
	virtual bool Use (bool pickup);

	virtual FState *GetUpState ();
	virtual FState *GetDownState ();
	virtual FState *GetReadyState ();
	virtual FState *GetAtkState ();
	virtual FState *GetHoldAtkState ();

	virtual void PostMorphWeapon ();
	virtual void EndPowerup ();

	enum
	{
		PrimaryFire,
		AltFire,
		EitherFire
	};
	bool CheckAmmo (int fireMode, bool autoSwitch, bool requireAmmo=false);
	bool DepleteAmmo (bool altFire, bool checkEnough=true);

protected:
	static AAmmo *AddAmmo (AActor *other, const PClass *ammotype, int amount);
	static bool AddExistingAmmo (AAmmo *ammo, int amount);
	AWeapon *AddWeapon (const PClass *weapon);
};

enum
{
	WIF_NOAUTOFIRE =		0x00000001, // weapon does not autofire
	WIF_READYSNDHALF =		0x00000002, // ready sound is played ~1/2 the time
	WIF_DONTBOB =			0x00000004, // don't bob the weapon
	WIF_AXEBLOOD =			0x00000008, // weapon makes axe blood on impact (Hexen only)
	WIF_NOALERT =			0x00000010,	// weapon does not alert monsters
	WIF_AMMO_OPTIONAL =		0x00000020, // weapon can use ammo but does not require it
	WIF_ALT_AMMO_OPTIONAL = 0x00000040, // alternate fire can use ammo but does not require it
	WIF_PRIMARY_USES_BOTH =	0x00000080, // primary fire uses both ammo
	WIF_ALT_USES_BOTH =		0x00000100, // alternate fire uses both ammo
	WIF_WIMPY_WEAPON =		0x00000200, // change away when ammo for another weapon is replenished
	WIF_POWERED_UP =		0x00000400, // this is a tome-of-power'ed version of its sister
	WIF_EXTREME_DEATH =		0x00000800,	// weapon always causes an extreme death

	WIF_STAFF2_KICKBACK =	0x00002000, // the powered-up Heretic staff has special kickback

	WIF_CHEATNOTWEAPON	=	1<<27,		// Give cheat considers this not a weapon (used by Sigil)

	// Flags used only by bot AI:

	WIF_BOT_REACTION_SKILL_THING = 1<<31, // I don't understand this
	WIF_BOT_EXPLOSIVE =		1<<30,		// weapon fires an explosive
	WIF_BOT_MELEE =			1<<29,		// melee weapon
	WIF_BOT_BFG =			1<<28,		// this is a BFG
};

#define S_LIGHTDONE 0

// Health is some item that gives the player health when picked up.
class AHealth : public AInventory
{
	DECLARE_STATELESS_ACTOR (AHealth, AInventory)

	int PrevHealth;
public:
	virtual bool TryPickup (AActor *other);
	virtual const char *PickupMessage ();
};

// HealthPickup is some item that gives the player health when used.
class AHealthPickup : public AInventory
{
	DECLARE_STATELESS_ACTOR (AHealthPickup, AInventory)
public:
	virtual AInventory *CreateCopy (AActor *other);
	virtual AInventory *CreateTossable ();
	virtual bool HandlePickup (AInventory *item);
	virtual bool Use (bool pickup);
};

// Armor absorbs some damage for the player.
class AArmor : public AInventory
{
	DECLARE_STATELESS_ACTOR (AArmor, AInventory)
};

// Basic armor absorbs a specific percent of the damage. You should
// never pickup a BasicArmor. Instead, you pickup a BasicArmorPickup
// or BasicArmorBonus and those gives you BasicArmor when it activates.
class ABasicArmor : public AArmor
{
	DECLARE_STATELESS_ACTOR (ABasicArmor, AArmor)
public:
	virtual void Serialize (FArchive &arc);
	virtual void Tick ();
	virtual AInventory *CreateCopy (AActor *other);
	virtual bool HandlePickup (AInventory *item);
	virtual void AbsorbDamage (int damage, int damageType, int &newdamage);

	fixed_t SavePercent;
};

// BasicArmorPickup replaces the armor you have.
class ABasicArmorPickup : public AArmor
{
	DECLARE_STATELESS_ACTOR (ABasicArmorPickup, AArmor)
public:
	virtual void Serialize (FArchive &arc);
	virtual AInventory *CreateCopy (AActor *other);
	virtual bool Use (bool pickup);

	fixed_t SavePercent;
	int SaveAmount;
};

// BasicArmorBonus adds to the armor you have.
class ABasicArmorBonus : public AArmor
{
	DECLARE_STATELESS_ACTOR (ABasicArmorBonus, AArmor)
public:
	virtual void Serialize (FArchive &arc);
	virtual AInventory *CreateCopy (AActor *other);
	virtual bool Use (bool pickup);

	fixed_t SavePercent;	// The default, for when you don't already have armor
	int MaxSaveAmount;
	int SaveAmount;
};

// Hexen armor consists of four separate armor types plus a conceptual armor
// type (the player himself) that work together as a single armor.
class AHexenArmor : public AArmor
{
	DECLARE_STATELESS_ACTOR (AHexenArmor, AArmor)
public:
	virtual void Serialize (FArchive &arc);
	virtual AInventory *CreateCopy (AActor *other);
	virtual AInventory *CreateTossable ();
	virtual bool HandlePickup (AInventory *item);
	virtual void AbsorbDamage (int damage, int damageType, int &newdamage);

	fixed_t Slots[5];
	fixed_t SlotsIncrement[4];

protected:
	bool AddArmorToSlot (AActor *actor, int slot, int amount);
};

// PuzzleItems work in conjunction with the UsePuzzleItem special
class APuzzleItem : public AInventory
{
	DECLARE_STATELESS_ACTOR (APuzzleItem, AInventory)
public:
	void Serialize (FArchive &arc);
	bool ShouldStay ();
	bool Use (bool pickup);
	bool HandlePickup (AInventory *item);

	int PuzzleItemNumber;
};

// A MapRevealer reveals the whole map for the player who picks it up.
class AMapRevealer : public AInventory
{
	DECLARE_STATELESS_ACTOR (AMapRevealer, AInventory)
public:
	bool TryPickup (AActor *toucher);
};

// A backpack gives you one clip of each ammo and doubles your
// normal maximum ammo amounts.
class ABackpack : public AInventory
{
	DECLARE_ACTOR (ABackpack, AInventory)
public:
	void Serialize (FArchive &arc);
	bool HandlePickup (AInventory *item);
	AInventory *CreateCopy (AActor *other);
	AInventory *CreateTossable ();
	void DetachFromOwner ();

	bool bDepleted;
};

// When the communicator is in a player's inventory, the
// SendToCommunicator special can work.
class ACommunicator : public AInventory
{
	DECLARE_ACTOR (ACommunicator, AInventory)
};

#endif //__A_PICKUPS_H__
