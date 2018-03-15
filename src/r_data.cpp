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
// Revision 1.3  1997/01/29 20:10
// DESCRIPTION:
//		Preparation of data for rendering,
//		generation of lookups, caching, retrieval by name.
//
//-----------------------------------------------------------------------------

// On the Alpha, accessing the shorts directly if they aren't aligned on a
// 4-byte boundary causes unaligned access warnings. Why it does this at
// all and only while initing the textures is beyond me.

#ifdef ALPHA
#define SAFESHORT(s)	((short)(((byte *)&(s))[0] + ((byte *)&(s))[1] * 256))
#else
#define SAFESHORT(s)	LittleShort(s)
#endif

#include <stddef.h>
#include <malloc.h>

#include "i_system.h"
#include "m_alloc.h"

#include "m_swap.h"
#include "m_png.h"

#include "w_wad.h"

#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"

#include "doomstat.h"
#include "r_sky.h"

#include "c_dispatch.h"
#include "c_console.h"
#include "r_data.h"

#include "v_palette.h"
#include "v_video.h"
#include "gi.h"
#include "cmdlib.h"
#include "templates.h"

extern float LastFOV;

static void R_InitPatches ();

// [RH] Just a format I invented to avoid WinTex's palette remapping
// when I wanted to insert some alpha maps.

struct FIMGZTexture::ImageHeader
{
	BYTE Magic[4];
	WORD Width;
	WORD Height;
	SWORD LeftOffset;
	SWORD TopOffset;
	BYTE Compression;
	BYTE Reserved[11];
};

//
// Graphics.
// DOOM graphics for walls and sprites
// is stored in vertical runs of opaque pixels (posts).
// A column is composed of zero or more posts,
// a patch or sprite is composed of zero or more columns.
// 

// for global animation
bool*			flatwarp;
byte**			warpedflats;
int*			flatwarpedwhen;


static TArray<BYTE *> BuildTileFiles;
FTextureManager TexMan;

FTextureManager::FTextureManager ()
{
	memset (HashFirst, -1, sizeof(HashFirst));
	// Texture 0 is a dummy texture used to indicate "no texture"
	AddTexture (new FDummyTexture);
}

FTextureManager::~FTextureManager ()
{
	for (unsigned int i = 0; i < Textures.Size(); ++i)
	{
		delete Textures[i].Texture;
	}
}

int FTextureManager::CheckForTexture (const char *name, int usetype, BITFIELD flags)
{
	int i;

	if (name == NULL || name[0] == '\0')
	{
		return -1;
	}
	// [RH] Doom counted anything beginning with '-' as "no texture".
	// Hopefully nobody made use of that and had textures like "-EMPTY",
	// because -NOFLAT- is a valid graphic for ZDoom.
	if (name[0] == '-' && name[1] == '\0')
	{
		return 0;
	}
	i = HashFirst[MakeKey (name) % HASH_SIZE];

	while (i != HASH_END)
	{
		const FTexture *tex = Textures[i].Texture;

		if (stricmp (tex->Name, name) == 0)
		{
			// The name matches, so check the texture type
			if (usetype == FTexture::TEX_Any)
			{
				return i;
			}
			else if ((flags & TEXMAN_Overridable) && tex->UseType == FTexture::TEX_Override)
			{
				return i;
			}
			else if (tex->UseType == usetype)
			{
				return i;
			}
		}
		i = Textures[i].HashNext;
	}

	if ((flags & TEXMAN_TryAny) && usetype != FTexture::TEX_Any)
	{
		return CheckForTexture (name, FTexture::TEX_Any, flags & ~TEXMAN_TryAny);
	}

	return -1;
}

int FTextureManager::GetTexture (const char *name, int usetype, BITFIELD flags)
{
	int i;

	if (name == NULL || name[0] == 0)
	{
		return 0;
	}
	else
	{
		i = CheckForTexture (name, usetype, flags | TEXMAN_TryAny);
	}

	if (i == -1)
	{
		// Use a default texture instead of aborting like Doom did
		Printf ("Unknown texture: \"%s\"\n", name);
		i = DefaultTexture;
	}
	return i;
}

void FTextureManager::WriteTexture (FArchive &arc, int picnum)
{
	FTexture *pic;

	if ((size_t)picnum >= Textures.Size())
	{
		pic = Textures[0].Texture;
	}
	else
	{
		pic = Textures[picnum].Texture;
	}

	arc.WriteCount (pic->UseType);
	arc.WriteName (pic->Name);
}

int FTextureManager::ReadTexture (FArchive &arc)
{
	int usetype;
	const char *name;

	usetype = arc.ReadCount ();
	name = arc.ReadName ();

	return GetTexture (name, usetype);
}

void FTextureManager::UnloadAll ()
{
	for (unsigned int i = 0; i < Textures.Size(); ++i)
	{
		Textures[i].Texture->Unload ();
	}
}

int FTextureManager::AddTexture (FTexture *texture)
{
	// Later textures take precedence over earlier ones
	size_t bucket = MakeKey (texture->Name) % HASH_SIZE;
	TextureHash hasher = { texture, HashFirst[bucket] };
	WORD trans = Textures.Push (hasher);
	Translation.Push (trans);
	HashFirst[bucket] = trans;
	return trans;
}

// Examines the lump contents to decide what type of texture to create,
// creates the texture, and adds it to the manager.
int FTextureManager::CreateTexture (int lumpnum, int usetype)
{
	FTexture *out = NULL;
    enum { t_patch, t_raw, t_imgz, t_png } type = t_patch;
	union
	{
		DWORD dw;
		WORD w[2];
	} first4bytes;

	// Must check the length of the lump. Zero length flat markers (F1_START etc.) will come through here.
	// 13 is the minimum length of anything valid (i.e a 1x1 pixel Doom patch.)
	if (lumpnum < 0 || Wads.LumpLength(lumpnum)<13)
	{
		return -1;
	}
	else
	{
		FWadLump data = Wads.OpenLumpNum (lumpnum);

		data >> first4bytes.dw;
		if (first4bytes.dw == MAKE_ID('I','M','G','Z'))
		{
			type = t_imgz;
		}
		else if (first4bytes.dw == MAKE_ID(137,'P','N','G'))
		{
			DWORD width, height;
			BYTE bitdepth, colortype, compression, filter, interlace;

			// This is most likely a PNG, but make sure. (Note that if the
			// first 4 bytes match, but later bytes don't, we assume it's
			// a corrupt PNG.)
			data >> first4bytes.dw;
			if (first4bytes.dw != MAKE_ID(13,10,26,10))		return -1;
			data >> first4bytes.dw;
			if (first4bytes.dw != MAKE_ID(0,0,0,13))		return -1;
			data >> first4bytes.dw;
			if (first4bytes.dw != MAKE_ID('I','H','D','R'))	return -1;

			// The PNG looks valid so far. Check the IHDR to make sure it's a
			// type of PNG we support.
			data >> width >> height
				 >> bitdepth >> colortype >> compression >> filter >> interlace;

			if (compression != 0 || filter != 0 || interlace != 0)
			{
				return -1;
			}
			if (!((1 << colortype) & 0x5D))
			{
				return -1;
			}
			if (!((1 << bitdepth) & 0x116))
			{
				return -1;
			}

			// Just for completeness, make sure the PNG has something more than an
			// IHDR.
			data >> first4bytes.dw >> first4bytes.dw;
			if (first4bytes.dw == 0)
			{
				data >> first4bytes.dw;
				if (first4bytes.dw == MAKE_ID('I','E','N','D'))
				{
					return -1;
				}
			}

			type = t_png;
			out = new FPNGTexture (lumpnum, BigLong((int)width), BigLong((int)height),
				bitdepth, colortype, interlace);
		}
		else if (usetype == FTexture::TEX_Flat)
		{
			// allow PNGs as flats but not Doom patches.
			return -1;
		}
		else if ((gameinfo.flags & GI_PAGESARERAW) && data.GetLength() == 64000)
		{
			// This is probably a raw page graphic, but do some checking to be sure
			patch_t *foo;
			int height;
			int width;

			foo = (patch_t *)M_Malloc (data.GetLength());
			data.Seek (-4, SEEK_CUR);
			data.Read (foo, data.GetLength());

			height = LittleShort(foo->height);
			width = LittleShort(foo->width);

			if (height > 0 && height < 510 && width > 0 && width < 15997)
			{
				// The dimensions seem like they might be valid for a patch, so
				// check the column directory for extra security. At least one
				// column must begin exactly at the end of the column directory,
				// and none of them must point past the end of the patch.
				bool gapAtStart = true;
				int x;

				for (x = 0; x < width; ++x)
				{
					DWORD ofs = LittleLong(foo->columnofs[x]);
					if (ofs == (DWORD)width * 4 + 8)
					{
						gapAtStart = false;
					}
					else if (ofs >= 64000-1)	// Need one byte for an empty column
					{
						break;
					}
					else
					{
						// Ensure this column does not extend beyond the end of the patch
						const BYTE *foo2 = (const BYTE *)foo;
						while (ofs < 64000)
						{
							if (foo2[ofs] == 255)
							{
								break;
							}
							ofs += foo2[ofs+1] + 4;
						}
						if (ofs >= 64000)
						{
							break;
						}
					}
				}
				if (gapAtStart || (x != width))
				{
					type = t_raw;
				}
			}
			else
			{
				type = t_raw;
			}
			free (foo);
		}
	}
	switch (type)
	{
	default:
		{	// Check patch sizes for sanity
			WORD width = LittleShort(first4bytes.w[0]);
			WORD height = LittleShort(first4bytes.w[1]);

			if (width <= 2048 && height <= 2048)
			{
				out = new FPatchTexture (lumpnum, FTexture::TEX_MiscPatch);
			}
		}
		break;
	case t_raw:		out = new FRawPageTexture (lumpnum); break;
	case t_imgz:	out = new FIMGZTexture (lumpnum); break;
	case t_png:		break;
	}
	if (out != NULL)
	{
		if (usetype != FTexture::TEX_Any)
		{
			out->UseType = usetype;
		}
		return AddTexture (out);
	}
	return -1;
}

void FTextureManager::ReplaceTexture (int picnum, FTexture *newtexture, bool free)
{
	if ((size_t)picnum >= Textures.Size())
		return;

	FTexture *oldtexture = Textures[picnum].Texture;

	strcpy (newtexture->Name, oldtexture->Name);
	newtexture->UseType = oldtexture->UseType;
	Textures[picnum].Texture = newtexture;

	if (free)
	{
		delete oldtexture;
	}
}

int FTextureManager::AddPatch (const char *patchname, int namespc)
{
	if (patchname == NULL)
	{
		return -1;
	}
	int lumpnum = CheckForTexture (patchname, FTexture::TEX_MiscPatch, false);

	if (lumpnum >= 0)
	{
		return lumpnum;
	}
	lumpnum = Wads.CheckNumForName (patchname, namespc==ns_global? ns_graphics:namespc);
	if (lumpnum < 0)
	{
		return -1;
	}

	return CreateTexture (lumpnum);
}

void FTextureManager::AddFlats ()
{
	int firstflat = Wads.GetNumForName ("F_START") + 1;
	int lastflat = Wads.GetNumForName ("F_END") - 1;
	int i;

	for (i = firstflat; i <= lastflat; ++i)
	{
		// Support PNGs as flats!
		if (CreateTexture (i, FTexture::TEX_Flat) == -1)
		{
			AddTexture (new FFlatTexture (i));
		}
	}
}

void FTextureManager::AddSprites ()
{
	int firstsprite = Wads.GetNumForName ("S_START") + 1;
	int lastsprite = Wads.GetNumForName ("S_END") - 1;
	int i;

	for (i = firstsprite; i <= lastsprite; ++i)
	{
		CreateTexture (i, FTexture::TEX_Sprite);
	}
}

void FTextureManager::AddTiles (void *tiles)
{
//	int numtiles = LittleLong(((DWORD *)tiles)[1]);	// This value is not reliable
	int tilestart = LittleLong(((DWORD *)tiles)[2]);
	int tileend = LittleLong(((DWORD *)tiles)[3]);
	const WORD *tilesizx = &((const WORD *)tiles)[8];
	const WORD *tilesizy = &tilesizx[tileend - tilestart + 1];
	const DWORD *picanm = (const DWORD *)&tilesizy[tileend - tilestart + 1];
	BYTE *tiledata = (BYTE *)&picanm[tileend - tilestart + 1];

	for (int i = tilestart; i <= tileend; ++i)
	{
		int pic = i - tilestart;
		int width = LittleShort(tilesizx[pic]);
		int height = LittleShort(tilesizy[pic]);
		DWORD anm = LittleLong(picanm[pic]);
		int xoffs = (SBYTE)((anm >> 8) & 255) + width/2;
		int yoffs = (SBYTE)((anm >> 16) & 255) + height/2;
		int size = width*height;
		int texnum;
		FTexture *tex;

		if (width <= 0 || height <= 0) continue;

		tex = new FBuildTexture (i, tiledata, width, height, xoffs, yoffs);
		texnum = AddTexture (tex);
		while (size > 0)
		{
			*tiledata = 255 - *tiledata;
			tiledata++;
			size--;
		}

		if ((picanm[pic] & 63) && (picanm[pic] & 192))
		{
			int type, speed;

			switch (picanm[pic] & 192)
			{
			case 64:	type = 2;	break;
			case 128:	type = 0;	break;
			case 192:	type = 1;	break;
			default:    type = 0;   break;  // Won't happen, but GCC bugs me if I don't put this here.
			}

			speed = (anm >> 24) & 15;
			speed = MAX (1, (1 << speed) * 1000 / 120);	// Convert from 120 Hz to 1000 Hz.

			R_AddSimpleAnim (texnum, picanm[pic] & 63, type, speed);
		}

		// Blood's rotation types:
		// 0 - Single
		// 1 - 5 Full
		// 2 - 8 Full
		// 3 - Bounce (looks no different from Single; seems to signal bouncy sprites)
		// 4 - 5 Half (not used in game)
		// 5 - 3 Flat (not used in game)
		// 6 - Voxel
		// 7 - Spin Voxel

		int rotType = (anm >> 28) & 7;
		if (rotType == 1)
		{
			spriteframe_t rot;
			rot.Texture[0] = texnum;
			rot.Texture[1] = texnum;
			for (int j = 1; j < 4; ++j)
			{
				rot.Texture[j*2] = texnum + j;
				rot.Texture[j*2+1] = texnum + j;
				rot.Texture[16-j*2] = texnum + j;
				rot.Texture[17-j*2] = texnum + j;
			}
			rot.Texture[8] = texnum + 4;
			rot.Texture[9] = texnum + 4;
			rot.Flip = 0x00FC;
			tex->Rotations = SpriteFrames.Push (rot);
		}
		else if (rotType == 2)
		{
			spriteframe_t rot;
			rot.Texture[0] = texnum;
			rot.Texture[1] = texnum;
			for (int j = 1; j < 8; ++j)
			{
				rot.Texture[16-j*2] = texnum + j;
				rot.Texture[17-j*2] = texnum + j;
			}
			rot.Flip = 0;
			tex->Rotations = SpriteFrames.Push (rot);
		}
	}
}

void FTextureManager::AddExtraTextures ()
{
	int firsttx = Wads.CheckNumForName ("TX_START");
	int lasttx = Wads.CheckNumForName ("TX_END");
	char name[9];

	if (firsttx == -1 || lasttx == -1)
	{
		return;
	}

	name[8] = 0;

	// Go from first to last so that ANIMDEFS work as expected. However,
	// to avoid duplicates (and to keep earlier entries from overriding
	// later ones), the texture is only inserted if it is the one returned
	// by doing a check by name in the list of wads.

	for (firsttx += 1; firsttx < lasttx; ++firsttx)
	{
		Wads.GetLumpName (name, firsttx);

		if (Wads.CheckNumForName (name, ns_newtextures) == firsttx)
		{
			CreateTexture (firsttx, FTexture::TEX_Override);
		}
	}
	DefaultTexture = CheckForTexture ("-NOFLAT-", FTexture::TEX_Override, 0);
}

void FTextureManager::AddPatches (int lumpnum)
{
	FWadLump *file = Wads.ReopenLumpNum (lumpnum);
	DWORD numpatches, i;
	char name[9];

	*file >> numpatches;
	name[8] = 0;

	for (i = 0; i < numpatches; ++i)
	{
		file->Read (name, 8);

		if (CheckForTexture (name, FTexture::TEX_WallPatch, false) == -1)
		{
			CreateTexture (Wads.CheckNumForName (name, ns_patches), FTexture::TEX_WallPatch);
		}
	}

	delete file;
}

void FTextureManager::AddTexturesLumps (int lump1, int lump2, int patcheslump)
{
	int firstdup = (int)Textures.Size();

	if (lump1 >= 0)
	{
		FMemLump texdir = Wads.ReadLump (lump1);
		AddTexturesLump (texdir.GetMem(), Wads.LumpLength (lump1), patcheslump, firstdup, true);
	}
	if (lump2 >= 0)
	{
		FMemLump texdir = Wads.ReadLump (lump2);
		AddTexturesLump (texdir.GetMem(), Wads.LumpLength (lump2), patcheslump, firstdup, false);
	}
}

void FTextureManager::AddTexturesLump (const void *lumpdata, int lumpsize, int patcheslump, int firstdup, bool texture1)
{
	FPatchLookup *patchlookup;
	int i, j;
	DWORD numpatches;

	if (firstdup == 0)
	{
		firstdup = (int)Textures.Size();
	}

	{
		FWadLump pnames = Wads.OpenLumpNum (patcheslump);

		pnames >> numpatches;

		// Check whether the amount of names reported is correct.
		if (numpatches < 0)
		{
			I_Error("Corrupt PNAMES lump found (negative amount of entries reported)");
			return;
		}

		// Check whether the amount of names reported is correct.
		int lumplength = Wads.LumpLength(patcheslump);
		if (numpatches > DWORD((lumplength-4)/8))
		{
			Printf("PNAMES lump is shorter than required (%ld entries reported but only %d bytes (%d entries) long\n",
				numpatches, lumplength, (lumplength-4)/8);
			// Truncate but continue reading. Who knows how many such lumps exist?
			numpatches = (lumplength-4)/8;
		}

		// Catalog the patches these textures use so we know which
		// textures they represent.
		patchlookup = (FPatchLookup *)alloca (numpatches * sizeof(*patchlookup));

		for (DWORD i = 0; i < numpatches; ++i)
		{
			pnames.Read (patchlookup[i].Name, 8);
			patchlookup[i].Name[8] = 0;

			j = CheckForTexture (patchlookup[i].Name, FTexture::TEX_WallPatch);
			if (j >= 0)
			{
				patchlookup[i].Texture = Textures[j].Texture;
			}
			else
			{
				// Shareware Doom has the same PNAMES lump as the registered
				// Doom, so printing warnings for patches that don't really
				// exist isn't such a good idea.
				//Printf ("Patch %s not found.\n", patchlookup[i].Name);
				patchlookup[i].Texture = NULL;
			}
		}
	}

	bool isStrife = false;
	const DWORD *maptex, *directory;
	DWORD maxoff;
	int numtextures;
	DWORD offset = 0;   // Shut up, GCC!

	maptex = (const DWORD *)lumpdata;
	numtextures = LittleLong(*maptex);
	maxoff = lumpsize;

	if (maxoff < DWORD(numtextures+1)*4)
	{
		I_FatalError ("Texture directory is too short");
	}

	// Scan the texture lump to decide if it contains Doom or Strife textures
	for (i = 0, directory = maptex+1; i < numtextures; ++i)
	{
		offset = LittleLong(directory[i]);
		if (offset > maxoff)
		{
			I_FatalError ("Bad texture directory");
		}

		maptexture_t *tex = (maptexture_t *)((BYTE *)maptex + offset);

		// There is bizzarely a Doom editing tool that writes to the
		// first two elements of columndirectory, so I can't check those.
		if (SAFESHORT(tex->patchcount) <= 0 ||
			tex->columndirectory[2] != 0 ||
			tex->columndirectory[3] != 0)
		{
			isStrife = true;
			break;
		}
	}


	// Textures defined earlier in the lump take precedence over those defined later,
	// but later TEXTUREx lumps take precedence over earlier ones.
	for (i = 1, directory = maptex; i <= numtextures; ++i)
	{
		if (i == 1 && texture1)
		{
			// The very first texture is just a dummy. Copy its dimensions to texture 0.
			// It still needs to be created in case someone uses it by name.
			offset = LittleLong(directory[1]);
			const maptexture_t *tex = (const maptexture_t *)((const BYTE *)maptex + offset);
			FDummyTexture *tex0 = static_cast<FDummyTexture *>(Textures[0].Texture);
			tex0->SetSize (SAFESHORT(tex->width), SAFESHORT(tex->height));
		}

		offset = LittleLong(directory[i]);
		if (offset > maxoff)
		{
			I_FatalError ("Bad texture directory");
		}

		// If this texture was defined already in this lump, skip it
		// This could cause problems with animations that use the same name for intermediate
		// textures. Should I be worried?
		for (j = (int)Textures.Size() - 1; j >= firstdup; --j)
		{
			if (strnicmp (Textures[j].Texture->Name, (const char *)maptex + offset, 8) == 0)
				break;
		}
		if (j + 1 == firstdup)
		{
			FTexture *tex = new FMultiPatchTexture ((const BYTE *)maptex + offset, patchlookup, numpatches, isStrife);
			if (i == 1 && texture1)
			{
				tex->UseType = FTexture::TEX_Null;
			}
			AddTexture (tex);
		}
	}
}




FTexture::FTexture ()
: LeftOffset(0), TopOffset(0),
  WidthBits(0), HeightBits(0), ScaleX(8), ScaleY(8),
  UseType(TEX_Any), bNoDecals(false), bNoRemap0(false), bWorldPanning(false),
  bMasked(true), bAlphaTexture(false), bHasCanvas(false), bWarped(0),
  Rotations(0xFFFF), Width(0xFFFF), Height(0), WidthMask(0)
{
	*Name=0;
}

FTexture::~FTexture ()
{
}

bool FTexture::CheckModified ()
{
	return false;
}

void FTexture::GetDimensions ()
{
	Width = 0;
	Height = 0;
}

void FTexture::SetFrontSkyLayer ()
{
	bNoRemap0 = true;
}

void FTexture::CalcBitSize ()
{
	// WidthBits is rounded down, and HeightBits is rounded up
	int i;

	for (i = 0; (1 << i) < Width; ++i)
	{ }

	WidthBits = i;

	// Having WidthBits that would allow for columns past the end of the
	// texture is not allowed, even if it means the entire texture is
	// not drawn.
	if (Width < (1 << WidthBits))
	{
		WidthBits--;
	}
	WidthMask = (1 << WidthBits) - 1;

	// The minimum height is 2, because we cannot shift right 32 bits.
	for (i = 1; (1 << i) < Height; ++i)
	{ }

	HeightBits = i;
}

FTexture::Span **FTexture::CreateSpans (const BYTE *pixels) const
{
	Span **spans, *span;

	if (!bMasked)
	{ // Texture does not have holes, so it can use a simpler span structure
		spans = (Span **)M_Malloc (sizeof(Span*)*Width + sizeof(Span)*2);
		span = (Span *)&spans[Width];
		for (int x = 0; x < Width; ++x)
		{
			spans[x] = span;
		}
		span[0].Length = Height;
		span[0].TopOffset = 0;
		span[1].Length = 0;
		span[1].TopOffset = 0;
	}
	else
	{ // Texture might have holes, so build a complete span structure
		int numcols = Width;
		int numrows = Height;
		int numspans = numcols;	// One span to terminate each column
		const BYTE *data_p;
		bool newspan;
		int x, y;

		data_p = pixels;

		// Count the number of spans in this texture
		for (x = numcols; x > 0; --x)
		{
			newspan = true;
			for (y = numrows; y > 0; --y)
			{
				if (*data_p++ == 0)
				{
					if (!newspan)
					{
						newspan = true;
					}
				}
				else if (newspan)
				{
					newspan = false;
					numspans++;
				}
			}
		}

		// Allocate space for the spans
		spans = (Span **)M_Malloc (sizeof(Span*)*numcols + sizeof(Span)*numspans);

		// Fill in the spans
		for (x = 0, span = (Span *)&spans[numcols], data_p = pixels; x < numcols; ++x)
		{
			newspan = true;
			spans[x] = span;
			for (y = 0; y < numrows; ++y)
			{
				if (*data_p++ == 0)
				{
					if (!newspan)
					{
						newspan = true;
						span++;
					}
				}
				else
				{
					if (newspan)
					{
						newspan = false;
						span->TopOffset = y;
						span->Length = 1;
					}
					else
					{
						span->Length++;
					}
				}
			}
			if (!newspan)
			{
				span++;
			}
			span->TopOffset = 0;
			span->Length = 0;
			span++;
		}
	}
	return spans;
}

void FTexture::FreeSpans (Span **spans) const
{
	free (spans);
}

void FTexture::CopyToBlock (BYTE *dest, int dwidth, int dheight, int xpos, int ypos, const BYTE *translation)
{
	int x1 = xpos, x2 = x1 + GetWidth(), xo = -x1;

	if (x1 < 0)
	{
		x1 = 0;
	}
	if (x2 > dwidth)
	{
		x2 = dwidth;
	}
	for (; x1 < x2; ++x1)
	{
		const BYTE *data;
		const Span *span;
		BYTE *outtop = &dest[dheight * x1];

		data = GetColumn (x1 + xo, &span);

		while (span->Length != 0)
		{
			int len = span->Length;
			int y = ypos + span->TopOffset;
			int adv = span->TopOffset;

			if (y < 0)
			{
				adv -= y;
				len += y;
				y = 0;
			}
			if (y + len > dheight)
			{
				len = dheight - y;
			}
			if (len > 0)
			{
				if (translation == NULL)
				{
					memcpy (outtop + y, data + adv, len);
				}
				else
				{
					for (int j = 0; j < len; ++j)
					{
						outtop[y+j] = translation[data[adv+j]];
					}
				}
			}
			span++;
		}
	}
}

// Converts a texture between row-major and column-major format
// by flipping it about the X=Y axis.

void FTexture::FlipSquareBlock (BYTE *block, int x, int y)
{
	int i, j;

	if (x != y) return;

	for (i = 0; i < x; ++i)
	{
		BYTE *corner = block + x*i + i;
		int count = x - i;
		if (count & 1)
		{
			count--;
			swap<BYTE> (corner[count], corner[count*x]);
		}
		for (j = 0; j < count; j += 2)
		{
			swap<BYTE> (corner[j], corner[j*x]);
			swap<BYTE> (corner[j+1], corner[(j+1)*x]);
		}
	}
}

void FTexture::FlipSquareBlockRemap (BYTE *block, int x, int y, const BYTE *remap)
{
	int i, j;
	BYTE t;

	if (x != y) return;

	for (i = 0; i < x; ++i)
	{
		BYTE *corner = block + x*i + i;
		int count = x - i;
		if (count & 1)
		{
			count--;
			t = remap[corner[count]];
			corner[count] = remap[corner[count*x]];
			corner[count*x] = t;
		}
		for (j = 0; j < count; j += 2)
		{
			t = remap[corner[j]];
			corner[j] = remap[corner[j*x]];
			corner[j*x] = t;
			t = remap[corner[j+1]];
			corner[j+1] = remap[corner[(j+1)*x]];
			corner[(j+1)*x] = t;
		}
	}
}

void FTexture::FlipNonSquareBlock (BYTE *dst, const BYTE *src, int x, int y, int srcpitch)
{
	int i, j;

	for (i = 0; i < x; ++i)
	{
		for (j = 0; j < y; ++j)
		{
			dst[i*y+j] = src[i+j*srcpitch];
		}
	}
}

void FTexture::FlipNonSquareBlockRemap (BYTE *dst, const BYTE *src, int x, int y, const BYTE *remap)
{
	int i, j;

	for (i = 0; i < x; ++i)
	{
		for (j = 0; j < y; ++j)
		{
			dst[i*y+j] = remap[src[i+j*x]];
		}
	}
}

FDummyTexture::FDummyTexture ()
{
	Width = 64;
	Height = 64;
	HeightBits = 6;
	WidthBits = 6;
	WidthMask = 63;
	Name[0] = 0;
	UseType = TEX_Null;
}

void FDummyTexture::Unload ()
{
}

void FDummyTexture::SetSize (int width, int height)
{
	Width = width;
	Height = height;
	CalcBitSize ();
}

// This must never be called
const BYTE *FDummyTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	return NULL;
}

// And this also must never be called
const BYTE *FDummyTexture::GetPixels ()
{
	return NULL;
}

FPatchTexture::FPatchTexture (int lumpnum, int usetype)
: SourceLump(lumpnum), Pixels(0), Spans(0)
{
	UseType = usetype;
	Wads.GetLumpName (Name, lumpnum);
	Name[8] = 0;
}

FPatchTexture::~FPatchTexture ()
{
	Unload ();
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
}

void FPatchTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FPatchTexture::GetPixels ()
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

const BYTE *FPatchTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

void FPatchTexture::GetDimensions ()
{
	// Wads.OpenLumpNum cannot be used here once Zip support has been added.
	// To work correctly it needs an assignment operator which
	// would cause considerable overhead elsewhere where it isn't needed.
	FWadLump * lump = Wads.ReopenLumpNum (SourceLump);
	patch_t dummy;

	(*lump) >> dummy.width >> dummy.height;

	if (dummy.width <= 0 || dummy.height <= 0 || dummy.width > 2048 || dummy.height > 2048)
	{
		delete lump;
		lump = Wads.ReopenLumpNum ( Wads.GetNumForName("-BADPATC", ns_graphics) );
		(*lump) >> dummy.width >> dummy.height;
	}

	(*lump) >> dummy.leftoffset >> dummy.topoffset;
	Width = dummy.width;
	Height = dummy.height;
	LeftOffset = dummy.leftoffset;
	TopOffset = dummy.topoffset;
	delete lump;

	CalcBitSize ();
}

void FPatchTexture::MakeTexture ()
{
	BYTE *remap, remaptable[256];
	Span *spanstuffer, *spanstarter;
	const column_t *maxcol;
	bool warned;
	int numspans;
	int x;

	FMemLump lump = Wads.ReadLump (SourceLump);
	const patch_t *patch = (const patch_t *)lump.GetMem();

	maxcol = (const column_t *)((const BYTE *)patch + Wads.LumpLength (SourceLump) - 3);

	// Check for badly-sized patches
	if (LittleShort(patch->width) <= 0 || LittleShort(patch->height) <= 0)
	{
		lump = Wads.ReadLump ("-BADPATC");
		patch = (const patch_t *)lump.GetMem();
		Printf (PRINT_BOLD, "Patch %s has a non-positive size.\n", Name);
	}
	else if (LittleShort(patch->width) > 2048 || LittleShort(patch->height) > 2048)
	{
		lump = Wads.ReadLump ("-BADPATC");
		patch = (const patch_t *)lump.GetMem();
		Printf (PRINT_BOLD, "Patch %s is too big.\n", Name);
	}

	if (Width == 0xFFFF)
	{
		Width = LittleShort(patch->width);
		Height = LittleShort(patch->height);
		LeftOffset = LittleShort(patch->leftoffset);
		TopOffset = LittleShort(patch->topoffset);
	}
	CalcBitSize ();

	// Add a little extra space at the end if the texture's height is not
	// a power of 2, in case somebody accidentally makes it repeat vertically.
	int numpix = Width * Height + (1 << HeightBits) - Height;

	numspans = Width;

	Pixels = new BYTE[numpix];
	memset (Pixels, 0, numpix);

	if (bNoRemap0)
	{
		memcpy (remaptable, GPalette.Remap, 256);
		remaptable[0] = 0;
		remap = remaptable;
	}
	else
	{
		remap = GPalette.Remap;
	}

	// Draw the image to the buffer
	for (x = 0; x < Width; ++x)
	{
		BYTE *outtop = Pixels + x*Height;
		const column_t *column = (const column_t *)((const BYTE *)patch + LittleLong(patch->columnofs[x]));
		int top = -1;

		while (column < maxcol && column->topdelta != 0xFF)
		{
			if (column->topdelta <= top)
			{
				top += column->topdelta;
			}
			else
			{
				top = column->topdelta;
			}

			int len = column->length;
			BYTE *out = outtop + top;

			if (len != 0)
			{
				if (top + len > Height)	// Clip posts that extend past the bottom
				{
					len = Height - top;
				}
				if (len > 0)
				{
					numspans++;

					const BYTE *in = (const BYTE *)column + 3;
					for (int i = 0; i < len; ++i)
					{
						out[i] = remap[in[i]];
					}
				}
			}
			column = (const column_t *)((const BYTE *)column + column->length + 4);
		}
	}

	// Create the spans
	if (Spans != NULL)
	{
		return;
	}

	Spans = (Span **)M_Malloc (sizeof(Span*)*Width + sizeof(Span)*numspans);
	spanstuffer = (Span *)((BYTE *)Spans + sizeof(Span*)*Width);
	warned = false;

	for (x = 0; x < Width; ++x)
	{
		const column_t *column = (const column_t *)((const BYTE *)patch + LittleLong(patch->columnofs[x]));
		int top = -1;

		Spans[x] = spanstuffer;
		spanstarter = spanstuffer;

		while (column < maxcol && column->topdelta != 0xFF)
		{
			if (column->topdelta <= top)
			{
				top += column->topdelta;
			}
			else
			{
				top = column->topdelta;
			}

			int len = column->length;

			if (len != 0)
			{
				if (top + len > Height)	// Clip posts that extend past the bottom
				{
					len = Height - top;
				}
				if (len > 0)
				{
					// There is something of this post to draw. If it starts at the same
					// place where the previous span ends, add it to that one. If it starts
					// before the other one ends, that's bad, but deal with it. If it starts
					// after the previous one ends, create another span.

					// Assume we need to create another span.
					spanstuffer->TopOffset = top;
					spanstuffer->Length = len;

					// Now check if that's really the case.
					if (spanstuffer > spanstarter)
					{
						if ((spanstuffer - 1)->TopOffset + (spanstuffer - 1)->Length == top)
						{
							(--spanstuffer)->Length += len;
						}
						else
						{
							int prevbot;

							while (spanstuffer > spanstarter &&
								spanstuffer->TopOffset < (prevbot = 
								(spanstuffer - 1)->TopOffset + (spanstuffer - 1)->Length))
							{
								if (spanstuffer->TopOffset < (spanstuffer - 1)->TopOffset)
								{
									(spanstuffer - 1)->TopOffset = spanstuffer->TopOffset;
								}
								(spanstuffer - 1)->Length = MAX(prevbot,
									spanstuffer->TopOffset + spanstuffer->Length)
									- (spanstuffer - 1)->TopOffset;
								spanstuffer--;
								if (!warned)
								{
									warned = true;
									Printf (PRINT_BOLD, "Patch %s is malformed.\n", Name);
								}
							}
						}
					}
					spanstuffer++;
				}
			}
			column = (const column_t *)((const BYTE *)column + column->length + 4);
		}

		spanstuffer->Length = spanstuffer->TopOffset = 0;
		spanstuffer++;
	}
}

// Fix for certain special patches on single-patch textures.
void FPatchTexture::HackHack (int newheight)
{
	BYTE *out;
	int x;

	Unload ();
	if (Spans != NULL)
	{
		FreeSpans (Spans);
	}

	{
		FMemLump lump = Wads.ReadLump (SourceLump);
		const patch_t *patch = (const patch_t *)lump.GetMem();

		Width = LittleShort(patch->width);
		Height = newheight;
		LeftOffset = 0;
		TopOffset = 0;

		Pixels = new BYTE[Width * Height];

		// Draw the image to the buffer
		for (x = 0, out = Pixels; x < Width; ++x)
		{
			const BYTE *in = (const BYTE *)patch + LittleLong(patch->columnofs[x]) + 3;

			for (int y = newheight; y > 0; --y)
			{
				*out = *in != 255 ? *in : Near255;
				out++, in++;
			}
			out += newheight;
		}
	}

	// Create the spans
	Spans = (Span **)M_Malloc (sizeof(Span *)*Width + sizeof(Span)*Width*2);

	Span *span = (Span *)&Spans[Width];

	for (x = 0; x < Width; ++x)
	{
		Spans[x] = span;
		span[0].Length = newheight;
		span[0].TopOffset = 0;
		span[1].Length = 0;
		span[1].TopOffset = 0;
		span += 2;
	}
}

FFlatTexture::FFlatTexture (int lumpnum)
: SourceLump(lumpnum), Pixels(0)
{
	int area;
	int bits;

	Wads.GetLumpName (Name, lumpnum);
	Name[8] = 0;
	area = Wads.LumpLength (lumpnum);

	switch (area)
	{
	default:
	case 64*64:		bits = 6;	break;
	case 8*8:		bits = 3;	break;
	case 16*16:		bits = 4;	break;
	case 32*32:		bits = 5;	break;
	case 128*128:	bits = 7;	break;
	case 256*256:	bits = 8;	break;
	}

	bMasked = false;
	WidthBits = HeightBits = bits;
	Width = Height = 1 << bits;
	WidthMask = (1 << bits) - 1;
	DummySpans[0].TopOffset = 0;
	DummySpans[0].Length = Height;
	DummySpans[1].TopOffset = 0;
	DummySpans[1].Length = 0;

	if (bits > 6)
	{
		ScaleX = ScaleY = 8 << (bits - 6);
	}
	else
	{
		ScaleX = ScaleY = 8;
	}

	UseType = TEX_Flat;
}

FFlatTexture::~FFlatTexture ()
{
	Unload ();
}

void FFlatTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FFlatTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = DummySpans;
	}
	return Pixels + column*Height;
}

const BYTE *FFlatTexture::GetPixels ()
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

void FFlatTexture::MakeTexture ()
{
	FWadLump lump = Wads.OpenLumpNum (SourceLump);
	Pixels = new BYTE[Width*Height];
	long numread = lump.Read (Pixels, Width*Height);
	if (numread < Width*Height)
	{
		memset (Pixels + numread, 0xBB, Width*Height - numread);
	}
	FlipSquareBlockRemap (Pixels, Width, Height, GPalette.Remap);
}

const FTexture::Span FRawPageTexture::DummySpans[2] =
{
	{ 0, 200 }, { 0, 0 }
};

FRawPageTexture::FRawPageTexture (int lumpnum)
: SourceLump(lumpnum), Pixels(0)
{
	Wads.GetLumpName (Name, lumpnum);
	Name[8] = 0;

	Width = 320;
	Height = 200;
	WidthBits = 8;
	HeightBits = 8;
	WidthMask = 255;

	UseType = TEX_MiscPatch;
}

FRawPageTexture::~FRawPageTexture ()
{
	Unload ();
}

void FRawPageTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FRawPageTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		column %= 320;
	}
	if (spans_out != NULL)
	{
		*spans_out = DummySpans;
	}
	return Pixels + column*Height;
}

const BYTE *FRawPageTexture::GetPixels ()
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

void FRawPageTexture::MakeTexture ()
{
	FMemLump lump = Wads.ReadLump (SourceLump);
	const BYTE *source = (const BYTE *)lump.GetMem();
	const BYTE *source_p = source;
	BYTE *dest_p;

	Pixels = new BYTE[Width*Height];
	dest_p = Pixels;

	// Convert the source image from row-major to column-major format
	for (int y = 200; y != 0; --y)
	{
		for (int x = 320; x != 0; --x)
		{
			*dest_p = GPalette.Remap[*source_p];
			dest_p += 200;
			source_p++;
		}
		dest_p -= 200*320-1;
	}
}

FIMGZTexture::FIMGZTexture (int lumpnum)
: SourceLump(lumpnum), Pixels(0), Spans(0)
{
	Wads.GetLumpName (Name, lumpnum);
	Name[8] = 0;

	UseType = TEX_MiscPatch;
}

FIMGZTexture::~FIMGZTexture ()
{
	Unload ();
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
}

void FIMGZTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FIMGZTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

const BYTE *FIMGZTexture::GetPixels ()
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

void FIMGZTexture::GetDimensions ()
{
	FWadLump lump = Wads.OpenLumpNum (SourceLump);
	DWORD magic;
	WORD w, h;
	SWORD l, t;

	lump >> magic >> w >> h >> l >> t;

	Width = w;
	Height = h;
	LeftOffset = l;
	TopOffset = t;
	CalcBitSize ();
}

void FIMGZTexture::MakeTexture ()
{
	FMemLump lump = Wads.ReadLump (SourceLump);
	const ImageHeader *imgz = (const ImageHeader *)lump.GetMem();
	const BYTE *data = (const BYTE *)&imgz[1];

	if (Width != 0xFFFF)
	{
		Width = LittleShort(imgz->Width);
		Height = LittleShort(imgz->Height);
		LeftOffset = LittleShort(imgz->LeftOffset);
		TopOffset = LittleShort(imgz->TopOffset);
	}

	BYTE *dest_p;
	int dest_adv = Height;
	int dest_rew = Width * Height - 1;

	CalcBitSize ();
	Pixels = new BYTE[Width*Height];
	dest_p = Pixels;

	// Convert the source image from row-major to column-major format
	if (!imgz->Compression)
	{
		for (int y = Height; y != 0; --y)
		{
			for (int x = Width; x != 0; --x)
			{
				*dest_p = *data;
				dest_p += dest_adv;
				data++;
			}
			dest_p -= dest_rew;
		}
	}
	else
	{
		// IMGZ compression is the same RLE used by IFF ILBM files
		int runlen = 0, setlen = 0;
		BYTE setval = 0;  // Shut up, GCC

		for (int y = Height; y != 0; --y)
		{
			for (int x = Width; x != 0; )
			{
				if (runlen != 0)
				{
					BYTE color = *data;
					*dest_p = color;
					dest_p += dest_adv;
					data++;
					x--;
					runlen--;
				}
				else if (setlen != 0)
				{
					*dest_p = setval;
					dest_p += dest_adv;
					x--;
					setlen--;
				}
				else
				{
					SBYTE code = *data++;
					if (code >= 0)
					{
						runlen = code + 1;
					}
					else if (code != -128)
					{
						setlen = (-code) + 1;
						setval = *data++;
					}
				}
			}
			dest_p -= dest_rew;
		}
	}

	if (Spans == NULL)
	{
		Spans = CreateSpans (Pixels);
	}
}


BYTE FPNGTexture::GrayMap[256];

FPNGTexture::FPNGTexture (int lumpnum, int width, int height,
						  BYTE depth, BYTE colortype, BYTE interlace)
: SourceLump(lumpnum), Pixels(0), Spans(0),
  BitDepth(depth), ColorType(colortype), Interlace(interlace),
  PaletteMap(0), PaletteSize(0), StartOfIDAT(0)
{
	union
	{
		DWORD palette[256];
		BYTE pngpal[256][3];
	};
	BYTE trans[256];
	bool havetRNS = false;
	DWORD len, id;
	int i;

	Wads.GetLumpName (Name, lumpnum);
	Name[8] = 0;

	UseType = TEX_MiscPatch;
	LeftOffset = 0;
	TopOffset = 0;
	bMasked = false;

	Width = width;
	Height = height;
	CalcBitSize ();

	memset (trans, 255, 256);

	// Parse pre-IDAT chunks. I skip the CRCs. Is that bad?
	FWadLump lump = Wads.OpenLumpNum (SourceLump);
	lump.Seek (33, SEEK_SET);

	lump >> len >> id;
	while (id != MAKE_ID('I','D','A','T') && id != MAKE_ID('I','E','N','D'))
	{
		len = BigLong((unsigned int)len);
		switch (id)
		{
		default:
			lump.Seek (len, SEEK_CUR);
			break;

		case MAKE_ID('g','r','A','b'):
			// This is like GRAB found in an ILBM, except coordinates use 4 bytes
			{
				DWORD hotx, hoty;

				lump >> hotx >> hoty;
				LeftOffset = BigLong((int)hotx);
				TopOffset = BigLong((int)hoty);
			}
			break;

		case MAKE_ID('P','L','T','E'):
			PaletteSize = MIN<int> (len / 3, 256);
			lump.Read (pngpal, PaletteSize * 3);
			if (PaletteSize * 3 != (int)len)
			{
				lump.Seek (len - PaletteSize * 3, SEEK_CUR);
			}
			for (i = PaletteSize - 1; i >= 0; --i)
			{
				palette[i] = MAKERGB(pngpal[i][0], pngpal[i][1], pngpal[i][2]);
			}
			break;

		case MAKE_ID('t','R','N','S'):
			lump.Read (trans, len);
			havetRNS = true;
			break;

		case MAKE_ID('a','l','P','h'):
			bAlphaTexture = true;
			bMasked = true;
			break;
		}
		lump >> len >> len;	// Skip CRC
		id = MAKE_ID('I','E','N','D');
		lump >> id;
	}
	StartOfIDAT = lump.Tell() - 8;

	switch (colortype)
	{
	case 4:		// Grayscale + Alpha
		bMasked = true;
		// intentional fall-through

	case 0:		// Grayscale
		if (!bAlphaTexture)
		{
			if (GrayMap[0] == GrayMap[255])
			{ // Initialize the GrayMap
				for (i = 0; i < 256; ++i)
				{
					GrayMap[i] = ColorMatcher.Pick (i, i, i);
				}
			}
			if (colortype == 0 && havetRNS && trans[0] != 0)
			{
				bMasked = true;
				PaletteSize = 256;
				PaletteMap = new BYTE[256];
				memcpy (PaletteMap, GrayMap, 256);
				PaletteMap[trans[0]] = 0;
			}
			else
			{
				PaletteMap = GrayMap;
			}
		}
		break;

	case 3:		// Paletted
		PaletteMap = new BYTE[PaletteSize];
		GPalette.MakeRemap (palette, PaletteMap, trans, PaletteSize);
		for (i = 0; i < PaletteSize; ++i)
		{
			if (trans[i] == 0)
			{
				bMasked = true;
				PaletteMap[i] = 0;
			}
		}
		break;

	case 6:		// RGB + Alpha
		bMasked = true;
		break;
	}
}

FPNGTexture::~FPNGTexture ()
{
	Unload ();
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
	if (PaletteMap != NULL && PaletteMap != GrayMap)
	{
		delete[] PaletteMap;
		PaletteMap = NULL;
	}
}

void FPNGTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FPNGTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

const BYTE *FPNGTexture::GetPixels ()
{
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

void FPNGTexture::MakeTexture ()
{
	FWadLump lump = Wads.OpenLumpNum (SourceLump);

	Pixels = new BYTE[Width*Height];
	if (StartOfIDAT == 0)
	{
		memset (Pixels, 0x99, Width*Height);
	}
	else
	{
		DWORD len, id;
		lump.Seek (StartOfIDAT, SEEK_SET);
		lump >> len >> id;

		if (ColorType == 0 || ColorType == 3)	/* Grayscale and paletted */
		{
			M_ReadIDAT (&lump, Pixels, Width, Height, Width, BitDepth, ColorType, Interlace, BigLong((unsigned int)len));

			if (Width == Height)
			{
				if (PaletteMap != NULL)
				{
					FlipSquareBlockRemap (Pixels, Width, Height, PaletteMap);
				}
				else
				{
					FlipSquareBlock (Pixels, Width, Height);
				}
			}
			else
			{
				BYTE *newpix = new BYTE[Width*Height];
				if (PaletteMap != NULL)
				{
					FlipNonSquareBlockRemap (newpix, Pixels, Width, Height, PaletteMap);
				}
				else
				{
					FlipNonSquareBlock (newpix, Pixels, Width, Height, Width);
				}
				BYTE *oldpix = Pixels;
				Pixels = newpix;
				delete[] oldpix;
			}
		}
		else		/* RGB and/or Alpha present */
		{
			int bytesPerPixel = ColorType == 2 ? 3 : ColorType == 4 ? 2 : 4;
			BYTE *tempix = new BYTE[Width * Height * bytesPerPixel];
			BYTE *in, *out;
			int x, y, pitch, backstep;

			M_ReadIDAT (&lump, tempix, Width, Height, Width*bytesPerPixel, BitDepth, ColorType, Interlace, BigLong((unsigned int)len));
			in = tempix;
			out = Pixels;

			// Convert from source format to paletted, column-major.
			// Formats with alpha maps are reduced to only 1 bit of alpha.
			switch (ColorType)
			{
			case 2:		// RGB
				pitch = Width * 3;
				backstep = Height * pitch - 3;
				for (x = Width; x > 0; --x)
				{
					for (y = Height; y > 0; --y)
					{
						*out++ = RGB32k[in[0]>>3][in[1]>>3][in[2]>>3];
						in += pitch;
					}
					in -= backstep;
				}
				break;

			case 4:		// Grayscale + Alpha
				pitch = Width * 2;
				backstep = Height * pitch - 2;
				if (PaletteMap != NULL)
				{
					for (x = Width; x > 0; --x)
					{
						for (y = Height; y > 0; --y)
						{
							*out++ = in[1] < 128 ? 0 : PaletteMap[in[0]];
							in += pitch;
						}
						in -= backstep;
					}
				}
				else
				{
					for (x = Width; x > 0; --x)
					{
						for (y = Height; y > 0; --y)
						{
							*out++ = in[1] < 128 ? 0 : in[0];
							in += pitch;
						}
						in -= backstep;
					}
				}
				break;

			case 6:		// RGB + Alpha
				pitch = Width * 4;
				backstep = Height * pitch - 4;
				for (x = Width; x > 0; --x)
				{
					for (y = Height; y > 0; --y)
					{
						*out++ = in[3] < 128 ? 0 : RGB32k[in[0]>>3][in[1]>>3][in[2]>>3];
						in += pitch;
					}
					in -= backstep;
				}
				break;
			}
			delete[] tempix;
		}
	}
	if (Spans == NULL)
	{
		Spans = CreateSpans (Pixels);
	}
}


FBuildTexture::FBuildTexture (int tilenum, const BYTE *pixels, int width, int height, int left, int top)
: Pixels (pixels), Spans (NULL)
{
	Width = width;
	Height = height;
	LeftOffset = left;
	TopOffset = top;
	CalcBitSize ();
	sprintf (Name, "BTIL%04d", tilenum);
	UseType = TEX_Build;
}

FBuildTexture::~FBuildTexture ()
{
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
}

void FBuildTexture::Unload ()
{
	// Nothing to do, since the pixels are accessed from memory-mapped files directly
}

const BYTE *FBuildTexture::GetPixels ()
{
	return Pixels;
}

const BYTE *FBuildTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (column >= Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		if (Spans == NULL)
		{
			Spans = CreateSpans (Pixels);
		}
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

FMultiPatchTexture::FMultiPatchTexture (const void *texdef, FPatchLookup *patchlookup, int maxpatchnum, bool strife)
: Pixels (0), Spans(0), Parts(0), bRedirect(false)
{
	union
	{
		const maptexture_t			*d;
		const strifemaptexture_t	*s;
	}
	mtexture;

	union
	{
		const mappatch_t			*d;
		const strifemappatch_t		*s;
	}
	mpatch;

	int i;

	mtexture.d = (const maptexture_t *)texdef;

	if (strife)
	{
		NumParts = SAFESHORT(mtexture.s->patchcount);
	}
	else
	{
		NumParts = SAFESHORT(mtexture.d->patchcount);
	}

	if (NumParts <= 0)
	{
		I_FatalError ("Bad texture directory");
	}

	UseType = FTexture::TEX_Wall;
	Parts = new TexPart[NumParts];
	Width = SAFESHORT(mtexture.d->width);
	Height = SAFESHORT(mtexture.d->height);
	strncpy (Name, mtexture.d->name, 8);
	Name[8] = 0;

	CalcBitSize ();

	// [RH] Special for beta 29: Values of 0 will use the tx/ty cvars
	// to determine scaling instead of defaulting to 8. I will likely
	// remove this once I finish the betas, because by then, users
	// should be able to actually create scaled textures.
	// 10-June-2003: It's still here long after beta 29. Heh.

	ScaleX = mtexture.d->ScaleX ? mtexture.d->ScaleX : 0;
	ScaleY = mtexture.d->ScaleY ? mtexture.d->ScaleY : 0;

	if (mtexture.d->Flags & MAPTEXF_WORLDPANNING)
	{
		bWorldPanning = true;
	}

	if (strife)
	{
		mpatch.s = &mtexture.s->patches[0];
	}
	else
	{
		mpatch.d = &mtexture.d->patches[0];
	}

	for (i = 0; i < NumParts; ++i)
	{
		if (unsigned(LittleShort(mpatch.d->patch)) >= unsigned(maxpatchnum))
		{
			I_FatalError ("Bad PNAMES and/or texture directory:\n\nPNAMES has %d entries, but\n%s wants to use entry %d.",
				maxpatchnum, Name, LittleShort(mpatch.d->patch)+1);
		}
		Parts[i].OriginX = LittleShort(mpatch.d->originx);
		Parts[i].OriginY = LittleShort(mpatch.d->originy);
		Parts[i].Texture = patchlookup[LittleShort(mpatch.d->patch)].Texture;
		if (Parts[i].Texture == NULL)
		{
			Printf ("Unknown patch %s in texture %s\n", patchlookup[LittleShort(mpatch.d->patch)].Name, Name);
			NumParts--;
			i--;
		}
		if (strife)
			mpatch.s++;
		else
			mpatch.d++;
	}
	if (NumParts == 0)
	{
		Printf ("Texture %s is left without any patches\n", Name);
	}

	CheckForHacks ();

	// If this texture is just a wrapper around a single patch, we can simply
	// forward GetPixels() and GetColumn() calls to that patch.
	if (NumParts == 1)
	{
		if (Parts->OriginX == 0 && Parts->OriginY == 0 &&
			Parts->Texture->GetWidth() == Width &&
			Parts->Texture->GetHeight() == Height)
		{
			bRedirect = true;
		}
	}
}

FMultiPatchTexture::~FMultiPatchTexture ()
{
	Unload ();
	if (Parts != NULL)
	{
		delete[] Parts;
		Parts = NULL;
	}
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
}

void FMultiPatchTexture::SetFrontSkyLayer ()
{
	for (int i = 0; i < NumParts; ++i)
	{
		Parts[i].Texture->SetFrontSkyLayer ();
	}
	bNoRemap0 = true;
}

void FMultiPatchTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
}

const BYTE *FMultiPatchTexture::GetPixels ()
{
	if (bRedirect)
	{
		return Parts->Texture->GetPixels ();
	}
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

const BYTE *FMultiPatchTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	if (bRedirect)
	{
		return Parts->Texture->GetColumn (column, spans_out);
	}
	if (Pixels == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

void FMultiPatchTexture::MakeTexture ()
{
	// Add a little extra space at the end if the texture's height is not
	// a power of 2, in case somebody accidentally makes it repeat vertically.
	int numpix = Width * Height + (1 << HeightBits) - Height;

	Pixels = new BYTE[numpix];
	memset (Pixels, 0, numpix);

	for (int i = 0; i < NumParts; ++i)
	{
		Parts[i].Texture->CopyToBlock (Pixels, Width, Height,
			Parts[i].OriginX, Parts[i].OriginY);
	}

	if (Spans == NULL)
	{
		Spans = CreateSpans (Pixels);
	}
}

void FMultiPatchTexture::CheckForHacks ()
{
	if (NumParts <= 0)
	{
		return;
	}

	// Heretic sky textures are marked as only 128 pixels tall,
	// even though they are really 200 pixels tall.
	if (gameinfo.gametype == GAME_Heretic &&
		Name[0] == 'S' &&
		Name[1] == 'K' &&
		Name[2] == 'Y' &&
		Name[4] == 0 &&
		Name[3] >= '1' &&
		Name[3] <= '3' &&
		Height == 128)
	{
		Height = 200;
		HeightBits = 8;
		return;
	}

	// The Doom E1 sky has its patch's y offset at -8 instead of 0.
	if (gameinfo.gametype == GAME_Doom &&
		!(gameinfo.flags & GI_MAPxx) &&
		NumParts == 1 &&
		Height == 128 &&
		Parts->OriginY == -8 &&
		Name[0] == 'S' &&
		Name[1] == 'K' &&
		Name[2] == 'Y' &&
		Name[3] == '1' &&
		Name[4] == 0)
	{
		Parts->OriginY = 0;
		return;
	}

	// BIGDOOR7 in Doom also has patches at y offset -4 instead of 0.
	if (gameinfo.gametype == GAME_Doom &&
		!(gameinfo.flags & GI_MAPxx) &&
		NumParts == 2 &&
		Height == 128 &&
		Parts[0].OriginY == -4 &&
		Parts[1].OriginY == -4 &&
		Name[0] == 'B' &&
		Name[1] == 'I' &&
		Name[2] == 'G' &&
		Name[3] == 'D' &&
		Name[4] == 'O' &&
		Name[5] == 'O' &&
		Name[6] == 'R' &&
		Name[7] == '7')
	{
		Parts[0].OriginY = 0;
		Parts[1].OriginY = 0;
		return;
	}

	// [RH] Some wads (I forget which!) have single-patch textures 256
	// pixels tall that have patch lengths recorded as 0. I can't think of
	// any good reason for them to do this, and since I didn't make note
	// of which wad made me hack in support for them, the hack is gone
	// because I've added support for DeePsea's true tall patches.
	//
	// Okay, I found a wad with crap patches: Pleiades.wad's sky patches almost
	// fit this description and are a big mess, but they're not single patch!
	if (Height == 256)
	{
		int i;

		// All patches must be at the top of the texture for this fix
		for (i = 0; i < NumParts; ++i)
		{
			if (Parts[i].OriginX != 0)
			{
				break;
			}
		}

		if (i == NumParts)
		{
			for (i = 0; i < NumParts; ++i)
			{
				FPatchTexture *tex = (FPatchTexture *)Parts[i].Texture;
				// Check if this patch is likely to be a problem.
				// It must be 256 pixels tall, and all its columns must have exactly
				// one post, where each post has a supposed length of 0.
				FMemLump lump = Wads.ReadLump (tex->SourceLump);
				const patch_t *realpatch = (patch_t *)lump.GetMem();
				const DWORD *cofs = realpatch->columnofs;
				int x, x2 = LittleShort(realpatch->width);

				if (LittleShort(realpatch->height) == 256)
				{
					for (x = 0; x < x2; ++x)
					{
						const column_t *col = (column_t*)((byte*)realpatch+LittleLong(cofs[x]));
						if (col->topdelta != 0 || col->length != 0)
						{
							break;	// It's not bad!
						}
						col = (column_t *)((byte *)col + 256 + 4);
						if (col->topdelta != 0xFF)
						{
							break;	// More than one post in a column!
						}
					}
					if (x == x2)
					{ // If all the columns were checked, it needs fixing.
						tex->HackHack (Height);
					}
				}
			}
		}
	}
}


FWarpTexture::FWarpTexture (FTexture *source)
: SourcePic (source), Pixels (0), Spans (0), GenTime (0)
{
	Width = source->GetWidth ();
	Height = source->GetHeight ();
	LeftOffset = source->LeftOffset;
	TopOffset = source->TopOffset;
	WidthBits = source->WidthBits;
	HeightBits = source->HeightBits;
	WidthMask = (1 << WidthBits) - 1;
	ScaleX = source->ScaleX;
	ScaleY = source->ScaleY;
	bNoDecals = source->bNoDecals;
	Rotations = source->Rotations;
	bWarped = 1;
}

FWarpTexture::~FWarpTexture ()
{
	Unload ();
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
	delete SourcePic;
}

void FWarpTexture::Unload ()
{
	if (Pixels != NULL)
	{
		delete[] Pixels;
		Pixels = NULL;
	}
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}
	SourcePic->Unload ();
}

bool FWarpTexture::CheckModified ()
{
	return r_FrameTime != GenTime;
}

const BYTE *FWarpTexture::GetPixels ()
{
	DWORD time = r_FrameTime;

	if (Pixels == NULL || time != GenTime)
	{
		MakeTexture (time);
	}
	return Pixels;
}

const BYTE *FWarpTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	DWORD time = r_FrameTime;

	if (Pixels == NULL || time != GenTime)
	{
		MakeTexture (time);
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		if (Spans == NULL)
		{
			Spans = CreateSpans (Pixels);
		}
		*spans_out = Spans[column];
	}
	return Pixels + column*Height;
}

void FWarpTexture::MakeTexture (DWORD time)
{
	const BYTE *otherpix = SourcePic->GetPixels ();

	if (Pixels == NULL)
	{
		Pixels = new BYTE[Width * Height];
	}
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}

	GenTime = time;

	byte *buffer = (byte *)alloca (MAX (Width, Height));
	int xsize = Width;
	int ysize = Height;
	int xmask = WidthMask;
	int ymask = Height - 1;
	int ybits = HeightBits;
	int x, y;

	if ((1 << ybits) > Height)
	{
		ybits--;
	}

	DWORD timebase = time * 32 / 28;
	for (y = ysize-1; y >= 0; y--)
	{
		int xt, xf = (finesine[(timebase+y*128)&FINEMASK]>>13) & xmask;
		const BYTE *source = otherpix + y;
		BYTE *dest = Pixels + y;
		for (xt = xsize; xt; xt--, xf = (xf+1)&xmask, dest += ysize)
			*dest = source[xf << ybits];
	}
	timebase = time * 23 / 28;
	for (x = xsize-1; x >= 0; x--)
	{
		int yt, yf = (finesine[(time+(x+17)*128)&FINEMASK]>>13) & ymask;
		const BYTE *source = Pixels + (x << ybits);
		BYTE *dest = buffer;
		for (yt = ysize; yt; yt--, yf = (yf+1)&ymask)
			*dest++ = source[yf];
		memcpy (Pixels+(x<<ybits), buffer, ysize);
	}
}

// [GRB] Eternity-like warping
FWarp2Texture::FWarp2Texture (FTexture *source)
: FWarpTexture (source)
{
	bWarped = 2;
}

void FWarp2Texture::MakeTexture (DWORD time)
{
	const BYTE *otherpix = SourcePic->GetPixels ();

	if (Pixels == NULL)
	{
		Pixels = new BYTE[Width * Height];
	}
	if (Spans != NULL)
	{
		FreeSpans (Spans);
		Spans = NULL;
	}

	GenTime = time;

	int xsize = Width;
	int ysize = Height;
	int xmask = WidthMask;
	int ymask = Height - 1;
	int ybits = HeightBits;
	int x, y;

	if ((1 << ybits) > Height)
	{
		ybits--;
	}

	DWORD timebase = time * 40 / 28;
	for (x = xsize-1; x >= 0; x--)
	{
		for (y = ysize-1; y >= 0; y--)
		{
			int xt = (x + 128
				+ ((finesine[(y*128 + timebase*5 + 900) & FINEMASK]*2)>>FRACBITS)
				+ ((finesine[(x*256 + timebase*4 + 300) & FINEMASK]*2)>>FRACBITS)) & xmask;
			int yt = (y + 128
				+ ((finesine[(y*128 + timebase*3 + 700) & FINEMASK]*2)>>FRACBITS)
				+ ((finesine[(x*256 + timebase*4 + 1200) & FINEMASK]*2)>>FRACBITS)) & ymask;
			const BYTE *source = otherpix + (xt << ybits) + yt;
			BYTE *dest = Pixels + (x << ybits) + y;
			*dest = *source;
		}
	}
}

FCanvasTexture::FCanvasTexture (const char *name, int width, int height)
{
	strncpy (Name, name, 8);
	Name[8] = 0;
	Width = width;
	Height = height;
	LeftOffset = TopOffset = 0;
	CalcBitSize ();

	bMasked = false;
	DummySpans[0].TopOffset = 0;
	DummySpans[0].Length = height;
	DummySpans[1].TopOffset = 0;
	DummySpans[1].Length = 0;
	UseType = TEX_Wall;
	Canvas = NULL;
	bNeedsUpdate = true;
	bDidUpdate = false;
	bHasCanvas = true;
	bFirstUpdate = true;
}

FCanvasTexture::~FCanvasTexture ()
{
	Unload ();
}

const BYTE *FCanvasTexture::GetColumn (unsigned int column, const Span **spans_out)
{
	bNeedsUpdate = true;
	if (Canvas == NULL)
	{
		MakeTexture ();
	}
	if ((unsigned)column >= (unsigned)Width)
	{
		if (WidthMask + 1 == Width)
		{
			column &= WidthMask;
		}
		else
		{
			column %= Width;
		}
	}
	if (spans_out != NULL)
	{
		*spans_out = DummySpans;
	}
	return Pixels + column*Height;
}

const BYTE *FCanvasTexture::GetPixels ()
{
	bNeedsUpdate = true;
	if (Canvas == NULL)
	{
		MakeTexture ();
	}
	return Pixels;
}

void FCanvasTexture::MakeTexture ()
{
	Canvas = new DSimpleCanvas (Width, Height);
	Canvas->Lock ();
	if (Width != Height || Width != Canvas->GetPitch())
	{
		Pixels = new BYTE[Width*Height];
	}
	else
	{
		Pixels = Canvas->GetBuffer();
	}
	// Draw a special "unrendered" initial texture into the buffer.
	memset (Pixels, 0, Width*Height/2);
	memset (Pixels+Width*Height/2, 255, Width*Height/2);
}

void FCanvasTexture::Unload ()
{
	if (Canvas != NULL)
	{
		if (Pixels != NULL && Pixels != Canvas->GetBuffer())
		{
			delete[] Pixels;
		}
		Pixels = NULL;
		delete Canvas;
		Canvas = NULL;
	}
}

bool FCanvasTexture::CheckModified ()
{
	if (bDidUpdate)
	{
		bDidUpdate = false;
		return true;
	}
	return false;
}

void FCanvasTexture::RenderView (AActor *viewpoint, int fov)
{
	if (Canvas == NULL)
	{
		MakeTexture ();
	}
	float savedfov = LastFOV;
	R_SetFOV (fov);
	R_RenderViewToCanvas (viewpoint, Canvas, 0, 0, Width, Height, bFirstUpdate);
	R_SetFOV (savedfov);
	if (Pixels == Canvas->GetBuffer())
	{
		FlipSquareBlock (Pixels, Width, Height);
	}
	else
	{
		FlipNonSquareBlock (Pixels, Canvas->GetBuffer(), Width, Height, Canvas->GetPitch());
	}
	bNeedsUpdate = false;
	bDidUpdate = true;
	bFirstUpdate = false;
}

//
// R_InitTextures
// Initializes the texture list with the textures from the world map.
//
void R_InitTextures (void)
{
	int lastlump = 0, lump;
	int texlump1 = -1, texlump2 = -1, texlump1a, texlump2a;
	int i;
	int pfile = -1;

	// For each PNAMES lump, load the TEXTURE1 and/or TEXTURE2 lumps from the same wad.
	while ((lump = Wads.FindLump ("PNAMES", &lastlump)) != -1)
	{
		pfile = Wads.GetLumpFile (lump);

		TexMan.AddPatches (lump);
		texlump1 = Wads.CheckNumForName ("TEXTURE1", ns_global, pfile);
		texlump2 = Wads.CheckNumForName ("TEXTURE2", ns_global, pfile);
		TexMan.AddTexturesLumps (texlump1, texlump2, lump);
	}

	// If the final TEXTURE1 and/or TEXTURE2 lumps are in a wad without a PNAMES lump,
	// they have not been loaded yet, so load them now.
	texlump1a = Wads.CheckNumForName ("TEXTURE1");
	texlump2a = Wads.CheckNumForName ("TEXTURE2");
	if (texlump1a != -1 && (texlump1a == texlump1 || Wads.GetLumpFile (texlump1a) <= pfile))
	{
		texlump1a = -1;
	}
	if (texlump2a != -1 && (texlump2a == texlump2 || Wads.GetLumpFile (texlump2a) <= pfile))
	{
		texlump2a = -1;
	}
	TexMan.AddTexturesLumps (texlump1a, texlump2a, Wads.GetNumForName ("PNAMES"));

	// The Hexen scripts use BLANK as a blank texture, even though it's really not.
	// I guess the Doom renderer must have clipped away the line at the bottom of
	// the texture so it wasn't visible. I'll just map it to 0, so it really is blank.
	if (gameinfo.gametype == GAME_Hexen &&
		0 <= (i = TexMan.CheckForTexture ("BLANK", FTexture::TEX_Wall, false)))
	{
		TexMan.SetTranslation (i, 0);
	}

	// Hexen parallax skies use color 0 to indicate transparency on the front
	// layer, so we must not remap color 0 on these textures. Unfortunately,
	// the only way to identify these textures is to check the MAPINFO.
	for (unsigned int i = 0; i < wadlevelinfos.Size(); ++i)
	{
		if (wadlevelinfos[i].flags & LEVEL_DOUBLESKY)
		{
			int picnum = TexMan.CheckForTexture (wadlevelinfos[i].skypic1, FTexture::TEX_Wall, false);
			if (picnum > 0)
			{
				TexMan[picnum]->SetFrontSkyLayer ();
			}
		}
	}
}

//
// R_InitBuildTiles
//
// [RH] Support Build tiles!
//

void R_InitBuildTiles ()
{
	int numartfiles = 0;
	char artfile[] = "tilesXXX.art";
	int lumpnum;

	lumpnum = Wads.CheckNumForFullName ("blood.pal");
	if (lumpnum >= 0)
	{
		// Blood's tiles are external resources. (Why did they do it like that?)
		FString rffpath = Wads.GetWadFullName (Wads.GetLumpFile (lumpnum));
		int slashat = rffpath.LastIndexOf ('/');
		if (slashat >= 0)
		{
			rffpath.Truncate (slashat + 1);
		}
		else
		{
			rffpath += '/';
		}

		for (; numartfiles < 1000; numartfiles++)
		{
			artfile[5] = numartfiles / 100 + '0';
			artfile[6] = numartfiles / 10 % 10 + '0';
			artfile[7] = numartfiles % 10 + '0';

			FString artpath = rffpath;
			artpath += artfile;

			FILE *f = fopen (artpath, "rb");
			if (f == NULL)
			{
				break;
			}

			size_t len = Q_filelength (f);
			BYTE *art = new BYTE[len];
			if (fread (art, 1, len, f) != len || LittleLong(*(DWORD *)art) != 1)
			{
				delete[] art;
			}
			else
			{
				BuildTileFiles.Push (art);
				TexMan.AddTiles (art);
			}
			fclose (f);
		}
	}

	for (; numartfiles < 1000; numartfiles++)
	{
		artfile[5] = numartfiles / 100 + '0';
		artfile[6] = numartfiles / 10 % 10 + '0';
		artfile[7] = numartfiles % 10 + '0';
		lumpnum = Wads.CheckNumForFullName (artfile);
		if (lumpnum < 0)
		{
			break;
		}

		BYTE *art = new BYTE[Wads.LumpLength (lumpnum)];
		Wads.ReadLump (lumpnum, art);

		if (LittleLong(*(DWORD *)art) != 1)
		{
			delete[] art;
		}
		else
		{
			BuildTileFiles.Push (art);
			TexMan.AddTiles (art);
		}
	}
}

void R_DeinitBuildTiles ()
{
	for (unsigned int i = 0; i < BuildTileFiles.Size(); ++i)
	{
		delete[] BuildTileFiles[i];
	}
	BuildTileFiles.Clear();
}

static struct FakeCmap {
	char name[8];
	PalEntry blend;
} *fakecmaps;
size_t numfakecmaps;
int firstfakecmap;
byte *realcolormaps;
int lastusedcolormap;

void R_SetDefaultColormap (const char *name)
{
	if (strnicmp (fakecmaps[0].name, name, 8) != 0)
	{
		int lump, i, j;
		BYTE map[256];
		BYTE unremap[256];
		BYTE remap[256];

		// [RH] If using BUILD's palette, generate the colormap
		if (Wads.CheckNumForFullName("palette.dat") >= 0 || Wads.CheckNumForFullName("blood.pal") >= 0)
		{
			Printf ("Make colormap\n");
			FDynamicColormap foo;

			foo.Color = 0xFFFFFF;
			foo.Fade = 0;
			foo.Maps = realcolormaps;
			foo.Desaturate = 0;
			foo.Next = NULL;
			foo.BuildLights ();
		}
		else
		{
			lump = Wads.CheckNumForName (name, ns_colormaps);
			if (lump == -1)
				lump = Wads.CheckNumForName (name, ns_global);
			FWadLump lumpr = Wads.OpenLumpNum (lump);

			// [RH] The colormap may not have been designed for the specific
			// palette we are using, so remap it to match the current palette.
			memcpy (remap, GPalette.Remap, 256);
			memset (unremap, 0, 256);
			for (i = 0; i < 256; ++i)
			{
				unremap[remap[i]] = i;
			}
			// Mapping to color 0 is okay, because the colormap won't be used to
			// produce a masked texture.
			remap[0] = 0;
			for (i = 0; i < NUMCOLORMAPS; ++i)
			{
				BYTE *map2 = &realcolormaps[i*256];
				lumpr.Read (map, 256);
				for (j = 0; j < 256; ++j)
				{
					map2[j] = remap[map[unremap[j]]];
				}
			}
		}

		uppercopy (fakecmaps[0].name, name);
		fakecmaps[0].blend = 0;
	}
}

//
// R_InitColormaps
//
void R_InitColormaps ()
{
	// [RH] Try and convert BOOM colormaps into blending values.
	//		This is a really rough hack, but it's better than
	//		not doing anything with them at all (right?)
	int lastfakecmap = Wads.CheckNumForName ("C_END");
	firstfakecmap = Wads.CheckNumForName ("C_START");

	if (firstfakecmap == -1 || lastfakecmap == -1)
		numfakecmaps = 1;
	else
		numfakecmaps = lastfakecmap - firstfakecmap;
	realcolormaps = new BYTE[256*NUMCOLORMAPS*numfakecmaps];
	fakecmaps = new FakeCmap[numfakecmaps];

	fakecmaps[0].name[0] = 0;
	R_SetDefaultColormap ("COLORMAP");

	if (numfakecmaps > 1)
	{
		BYTE unremap[256], remap[256], mapin[256];
		int i;
		size_t j;

		memcpy (remap, GPalette.Remap, 256);
		memset (unremap, 0, 256);
		for (i = 0; i < 256; ++i)
		{
			unremap[remap[i]] = i;
		}
		remap[0] = 0;
		for (i = ++firstfakecmap, j = 1; j < numfakecmaps; i++, j++)
		{
			if (Wads.LumpLength (i) >= (NUMCOLORMAPS+1)*256)
			{
				int k, r, g, b;
				FWadLump lump = Wads.OpenLumpNum (i);
				BYTE *const map = realcolormaps + NUMCOLORMAPS*256*j;

				for (k = 0; k < NUMCOLORMAPS; ++k)
				{
					BYTE *map2 = &map[k*256];
					lump.Read (mapin, 256);
					map2[0] = 0;
					for (r = 1; r < 256; ++r)
					{
						map2[r] = remap[mapin[unremap[r]]];
					}
				}

				r = g = b = 0;

				for (k = 0; k < 256; k++)
				{
					r += GPalette.BaseColors[map[k]].r;
					g += GPalette.BaseColors[map[k]].g;
					b += GPalette.BaseColors[map[k]].b;
				}
				Wads.GetLumpName (fakecmaps[j].name, i);
				fakecmaps[j].blend = PalEntry (255, r/256, g/256, b/256);
			}
		}
	}
}

void R_DeinitColormaps ()
{
	if (fakecmaps != NULL)
	{
		delete[] fakecmaps;
		fakecmaps = NULL;
	}
	if (realcolormaps != NULL)
	{
		delete[] realcolormaps;
		realcolormaps = NULL;
	}
}

// [RH] Returns an index into realcolormaps. Multiply it by
//		256*NUMCOLORMAPS to find the start of the colormap to use.
//		WATERMAP is an exception and returns a blending value instead.
DWORD R_ColormapNumForName (const char *name)
{
	int lump;
	DWORD blend = 0;

	if (strnicmp (name, "COLORMAP", 8))
	{	// COLORMAP always returns 0
		if (-1 != (lump = Wads.CheckNumForName (name, ns_colormaps)) )
			blend = lump - firstfakecmap + 1;
		else if (!strnicmp (name, "WATERMAP", 8))
			blend = MAKEARGB (128,0,0x4f,0xa5);
	}

	return blend;
}

DWORD R_BlendForColormap (DWORD map)
{
	return APART(map) ? map : 
		   map < numfakecmaps ? DWORD(fakecmaps[map].blend) : 0;
}

//
// R_InitData
// Locates all the lumps that will be used by all views
// Must be called after W_Init.
//
void R_InitData ()
{
	TexMan.AddSprites ();
	R_InitPatches ();
	R_InitTextures ();
	TexMan.AddFlats ();
	R_InitBuildTiles ();
	TexMan.AddExtraTextures ();

	R_InitColormaps ();
	C_InitConsole (SCREENWIDTH, SCREENHEIGHT, true);
}

void R_DeinitData ()
{
	R_DeinitColormaps ();
	R_DeinitBuildTiles();
	FCanvasTextureInfo::EmptyList();

	// Free openings
	if (openings != NULL)
	{
		free (openings);
		openings = NULL;
	}

	// Free drawsegs
	if (drawsegs != NULL)
	{
		free (drawsegs);
		drawsegs = NULL;
	}

}

//
// R_PrecacheLevel
// Preloads all relevant graphics for the level.
//

void R_PrecacheLevel (void)
{
	BYTE *hitlist;
	BYTE *spritelist;
	int i;

	if (demoplayback)
		return;

	hitlist = new BYTE[TexMan.NumTextures()];
	spritelist = new BYTE[sprites.Size()];
	
	// Precache textures (and sprites).
	memset (hitlist, 0, TexMan.NumTextures());
	memset (spritelist, 0, sprites.Size());

	{
		AActor *actor;
		TThinkerIterator<AActor> iterator;

		while ( (actor = iterator.Next ()) )
			spritelist[actor->sprite] = 1;
	}

	for (i = (int)(sprites.Size () - 1); i >= 0; i--)
	{
		if (spritelist[i])
		{
			int j, k;
			for (j = 0; j < sprites[i].numframes; j++)
			{
				const spriteframe_t *frame = &SpriteFrames[sprites[i].spriteframes + j];

				for (k = 0; k < 16; k++)
				{
					int pic = frame->Texture[k];
					if (pic != 0xFFFF)
					{
						hitlist[pic] = 1;
					}
				}
			}
		}
	}

	delete[] spritelist;

	for (i = numsectors - 1; i >= 0; i--)
	{
		hitlist[sectors[i].floorpic] = hitlist[sectors[i].ceilingpic] = 1;
	}

	for (i = numsides - 1; i >= 0; i--)
	{
		hitlist[sides[i].toptexture] =
			hitlist[sides[i].midtexture] =
			hitlist[sides[i].bottomtexture] = 1;
	}

	// Sky texture is always present.
	// Note that F_SKY1 is the name used to
	//	indicate a sky floor/ceiling as a flat,
	//	while the sky texture is stored like
	//	a wall texture, with an episode dependant
	//	name.

	if (sky1texture >= 0)
	{
		hitlist[sky1texture] = 1;
	}
	if (sky2texture >= 0)
	{
		hitlist[sky2texture] = 1;
	}

	for (i = TexMan.NumTextures() - 1; i >= 0; i--)
	{
		FTexture *tex = TexMan[i];
		if (tex != NULL)
		{
			if (hitlist[i])
			{
				tex->GetPixels ();
			}
			else
			{
				tex->Unload ();
			}
		}
	}

	delete[] hitlist;
}

const BYTE *R_GetColumn (FTexture *tex, int col)
{
	return tex->GetColumn (col, NULL);
}

// Add all the miscellaneous 2D patches that are used to the texture manager
// Unfortunately, the wad format does not provide an elegant way to express
// which lumps are patches unless they are used in a wall texture, so I have
// to list them all here.

static void R_InitPatches ()
{
	static const char patches[][9] = 
	{
		"CONBACK",
		"ADVISOR",
		"BOSSBACK",
		"PFUB1",
		"PFUB2",
		"END0",
		"END1",
		"END2",
		"END3",
		"END4",
		"END5",
		"END6",
		"FINALE1",
		"FINALE2",
		"FINALE3",
		"CHESSALL",
		"CHESSC",
		"CHESSM",
		"FITEFACE",
		"CLERFACE",
		"MAGEFACE",
		"M_NGAME",
		"M_OPTION",
		"M_RDTHIS",
		"M_QUITG",
		"M_JKILL",
		"M_ROUGH",
		"M_HURT",
		"M_ULTRA",
		"M_NMARE",
		"M_LOADG",
		"M_LSLEFT",
		"M_LSCNTR",
		"M_LSRGHT",
		"M_FSLOT",
		"M_SAVEG",
		"M_DOOM",
		"M_HTIC",
		"M_STRIFE",
		"M_NEWG",
		"M_NGAME",
		"M_SKILL",
		"M_EPISOD",
		"M_EPI1",
		"M_EPI2",
		"M_EPI3",
		"M_EPI4",
		"MAPE1",
		"MAPE2",
		"MAPE3",
		"WIMAP0",
		"WIURH0",
		"WIURH1",
		"WISPLAT",
		"WIMAP1",
		"WIMAP2",
		"INTERPIC",
		"WIOSTK",
		"WIOSTI",
		"WIF",
		"WIMSTT",
		"WIOSTS",
		"WIOSTF",
		"WITIME",
		"WIPAR",
		"WIMSTAR",
		"WIMINUS",
		"WIPCNT",
		"WICOLON",
		"WISUCKS",
		"WIFRGS",
		"WISCRT2",
		"WIENTER",
		"WIKILRS",
		"WIVCTMS",
		"IN_YAH",
		"IN_X",
		"FONTB13",
		"FONTB05",
		"FONTB26",
		"FONTB15",
		"FACEA0",
		"FACEB0",
		"STFDEAD0",
		"STBANY",
		"M_PAUSE",
		"PAUSED",
		"M_SKULL1",
		"M_SKULL2",
		"M_SLCTR1",
		"M_SLCTR2",
		"M_CURS1",
		"M_CURS2",
		"M_CURS3",
		"M_CURS4",
		"M_CURS5",
		"M_CURS6",
		"M_CURS7",
		"M_CURS8",
		"BRDR_TL",
		"BRDR_T",
		"BRDR_TR",
		"BRDR_L",
		"BRDR_R",
		"BRDR_BL",
		"BRDR_B",
		"BRDR_BR",
		"BORDTL",
		"BORDT",
		"BORDTR",
		"BORDL",
		"BORDR",
		"BORDBL",
		"BORDB",
		"BORDBR",
		"TITLE",
		"CREDIT",
		"ORDER",
		"HELP",
		"HELP1",
		"HELP2",
		"HELP3",
		"HELP0",
		"TITLEPIC",
		"ENDPIC",
		"FINALE1",
		"FINALE2",
		"FINALE3",
		"STTPRCNT",
		"STARMS",
		"VICTORY2",
		"STFBANY",
		"STPBANY",
		"RGELOGO",
		"VELLOGO",
		"FINAL1",
		"FINAL2",
		"E2END"
	};
	static const char spinners[][9] =
	{
		"SPINBK%d",
		"SPFLY%d",
		"SPSHLD%d",
		"SPBOOT%d",
		"SPMINO%d"
	};
	static const char classChars[3] = { 'F', 'C', 'M' };

	int i, j;
	char name[9];

	for (i = countof(patches); i >= 0; --i)
	{
		TexMan.AddPatch (patches[i]);
	}

	// Some digits
	for (i = 9; i >= 0; --i)
	{
		sprintf (name, "WINUM%d", i);
		TexMan.AddPatch (name);
		sprintf (name, "FONTB%d", i + 16);
		TexMan.AddPatch (name);
		sprintf (name, "AMMNUM%d", i);
		TexMan.AddPatch (name);
	}

	// Spinning power up icons for Heretic and Hexen
	for (j = countof(spinners)-1; j >= 0; --j)
	{
		for (i = 0; i <= 15; ++i)
		{
			sprintf (name, spinners[j], i);
			TexMan.AddPatch (name);
		}
	}

	// Animating overlays for the Doom E1 map
	for (i = 9; i >= 0; --i)
	{
		for (j = (i == 6) ? 3 : 2; j >= 0; --j)
		{
			sprintf (name, "WIA0%.2d%.2d", i, j);
			TexMan.AddPatch (name);
		}
	}
	// Animating overlays for the Doom E2 map
	for (i = 7; i >= 0; --i)
	{
		for (j = (i == 7) ? 2 : 0; j >= 0; --j)
		{
			sprintf (name, "WIA1%.2d%.2d", i, j);
			TexMan.AddPatch (name);
		}
	}
	// Animating overlays for the Doom E3 map
	for (i = 5; i >= 0; --i)
	{
		for (j = 2; j >= 0; --j)
		{
			sprintf (name, "WIA2%.2d%.2d", i, j);
			TexMan.AddPatch (name);
		}
	}

	// Player class animations for the Hexen new game menu
	for (i = 2; i >= 0; --i)
	{
		sprintf (name, "M_%cBOX", classChars[i]);
		TexMan.AddPatch (name);
		for (j = 4; j >= 1; --j)
		{
			sprintf (name, "M_%cWALK%d", classChars[i], j);
			TexMan.AddPatch (name);
		}
	}

	// The spinning skull in Heretic's top-level menu
	for (i = 0; i <= 17; ++i)
	{
		sprintf (name, "M_SKL%.2d", i);
		TexMan.AddPatch (name);
	}

	// Strife story panels
	for (i = 0; i <= 7; ++i)
	{
		sprintf (name, "PANEL%d", i);
		TexMan.AddPatch (name);
	}
	for (i = 2; i <= 6; ++i)
	{
		for (j = 3 + (i < 5); j > 0; --j)
		{
			sprintf (name, "SS%dF%d", i, j);
			TexMan.AddPatch (name);
		}
	}
}

#ifdef _DEBUG
// Prints the spans generated for a texture. Only needed for debugging.
CCMD (printspans)
{
	if (argv.argc() != 2)
		return;

	int picnum = TexMan.CheckForTexture (argv[1], FTexture::TEX_Any);
	if (picnum < 0)
	{
		Printf ("Unknown texture %s\n", argv[1]);
		return;
	}
	FTexture *tex = TexMan[picnum];
	for (int x = 0; x < tex->GetWidth(); ++x)
	{
		const FTexture::Span *spans;
		Printf ("%4d:", x);
		tex->GetColumn (x, &spans);
		while (spans->Length != 0)
		{
			Printf (" (%4d,%4d)", spans->TopOffset, spans->TopOffset+spans->Length-1);
			spans++;
		}
		Printf ("\n");
	}
}

CCMD (picnum)
{
	int picnum = TexMan.GetTexture (argv[1], FTexture::TEX_Any);
	Printf ("%d: %s - %s\n", picnum, TexMan[picnum]->Name, TexMan(picnum)->Name);
}
#endif
