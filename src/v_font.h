/*
** v_font.h
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

#ifndef __V_FONT_H__
#define __V_FONT_H__

#include "doomtype.h"
#include "farchive.h"

class DCanvas;
class FTexture;

enum EColorRange
{
	CR_BRICK,
	CR_TAN,
	CR_GRAY,
	CR_GREY = CR_GRAY,
	CR_GREEN,
	CR_BROWN,
	CR_GOLD,
	CR_RED,
	CR_BLUE,
	CR_ORANGE,
	CR_WHITE,
	CR_YELLOW,
	CR_UNTRANSLATED,
	NUM_TEXT_COLORS
};

inline FArchive &operator<< (FArchive &arc, EColorRange &i)
{
	BYTE val = (BYTE)i;
	arc << val;
	i = (EColorRange)val;
	return arc;
}

class FFont
{
public:
	FFont (const char *fontname, const char *nametemplate, int first, int count, int base);
	~FFont ();

	FTexture *GetChar (int code, int *const width) const;
	int GetCharWidth (int code) const;
	byte *GetColorTranslation (EColorRange range) const;
	int GetSpaceWidth () const { return SpaceWidth; }
	int GetHeight () const { return FontHeight; }
	int GetDefaultKerning () const { return GlobalKerning; }

	static FFont *FindFont (const char *fontname);

	// Return width of string in pixels (unscaled)
	int StringWidth (const byte *str) const;
	inline int StringWidth (const char *str) const { return StringWidth ((const byte *)str); }

protected:
	FFont ();

	void BuildTranslations (const double *luminosity, const BYTE *identity);

	static int SimpleTranslation (byte *colorsused, byte *translation, byte *identity, double **luminosity);

	int FirstChar, LastChar;
	int SpaceWidth;
	int FontHeight;
	int GlobalKerning;
	struct CharData
	{
		FTexture *Pic;
	} *Chars;
	int ActiveColors;
	BYTE *Ranges;
	BYTE *PatchRemap;

	char *Name;
	FFont *Next;

	static FFont *FirstFont;
	friend struct FontsDeleter;

	friend void V_Shutdown();

#if defined(_MSC_VER) && _MSC_VER < 1310
	template<> friend FArchive &operator<< (FArchive &arc, FFont* &font);
#else
	friend FArchive &SerializeFFontPtr (FArchive &arc, FFont* &font);
#endif
};

#if !defined(_MSC_VER) || _MSC_VER >= 1310
template<> inline FArchive &operator<< <FFont> (FArchive &arc, FFont* &font)
{
	return SerializeFFontPtr (arc, font);
}
#endif

class FSingleLumpFont : public FFont
{
public:
	FSingleLumpFont (const char *fontname, int lump);

protected:
	void BuildTranslations2 ();
	void FixupPalette (BYTE *identity, double *luminosity, const BYTE *palette, bool rescale);
	void LoadFON1 (int lump, const BYTE *data);
	void LoadFON2 (int lump, const BYTE *data);
	void CreateFontFromPic (int picnum);
};

void RecordTextureColors (FTexture *pic, byte *colorsused);

extern FFont *SmallFont, *SmallFont2, *BigFont, *ConFont;

void V_InitCustomFonts ();

#endif //__V_FONT_H__
