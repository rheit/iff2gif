/* This file is part of iff2gif.
**
** Copyright 2015-2019 - Marisa Heit
**
** iff2gif is free software : you can redistribute it and / or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** iff2gif is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with iff2gif. If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include "iff2gif.h"

ChunkyBitmap::ChunkyBitmap(const PlanarBitmap &planar, int scalex, int scaley)
{
	assert(scalex != 0);
	assert(scaley != 0);
	Alloc(planar.Width * scalex,
		  planar.Height * scaley,
		  planar.NumPlanes <= 8 ? 1 : planar.NumPlanes <= 16 ? 2 : 4);
	planar.ToChunky(Pixels, Width - planar.Width);
	if (scalex != 1 || scaley != 1)
	{
		Expand(scalex, scaley);
	}
}

ChunkyBitmap::ChunkyBitmap(int w, int h, int bpp)
{
	Alloc(w, h, bpp);
}

void ChunkyBitmap::Alloc(int w, int h, int bpp)
{
	assert(w != 0);
	assert(h != 0);
	assert(bpp == 1 || bpp == 2 || bpp == 4);
	Width = w;
	Height = h;
	BytesPerPixel = bpp;
	Pitch = Width * BytesPerPixel;
	Pixels = new uint8_t[Pitch * Height];
}

// Creates a new chunky bitmap with the same dimensions as o, but filled with fillcolor.
ChunkyBitmap::ChunkyBitmap(const ChunkyBitmap &o, int fillcolor)
	: Width(o.Width), Height(o.Height), Pitch(o.Pitch),
	  BytesPerPixel(o.BytesPerPixel)
{
	Pixels = new uint8_t[Pitch * Height];
	SetSolidColor(fillcolor);
}

ChunkyBitmap::~ChunkyBitmap()
{
	if (Pixels != nullptr)
	{
		delete[] Pixels;
	}
}

ChunkyBitmap::ChunkyBitmap(ChunkyBitmap &&o) noexcept
	: Width(o.Width), Height(o.Height), Pitch(o.Pitch),
	  BytesPerPixel(o.BytesPerPixel), Pixels(o.Pixels)
{
	o.Clear(false);
}

ChunkyBitmap &ChunkyBitmap::operator=(ChunkyBitmap &&o) noexcept
{
	if (&o != this)
	{
		if (Pixels != nullptr)
		{
			delete[] Pixels;
		}
		Width = o.Width;
		Height = o.Height;
		Pitch = o.Pitch;
		Pixels = o.Pixels;
		BytesPerPixel = o.BytesPerPixel;
		o.Clear(false);
	}
	return *this;
}

void ChunkyBitmap::Clear(bool release) noexcept
{
	if (release && Pixels != nullptr)
	{
		delete[] Pixels;
	}
	Pixels = nullptr;
	Width = 0;
	Height = 0;
	Pitch = 0;
	BytesPerPixel = 0;
}

void ChunkyBitmap::SetSolidColor(int color) noexcept
{
	if (Pixels != nullptr)
	{
		if (BytesPerPixel == 1)
		{
			memset(Pixels, color, Width * Height);
		}
		else if (BytesPerPixel == 2)
		{
			std::fill_n((uint16_t *)Pixels, Width * Height, (uint16_t)color);
		}
		else
		{
			std::fill_n((uint32_t *)Pixels, Width * Height, (uint32_t)color);
		}
	}
}

// Expansion is done in-place, with the original image located
// in the upper-left corner of the "destination" image.
void ChunkyBitmap::Expand(int scalex, int scaley) noexcept
{
	if (scalex == 1 && scaley == 1)
		return;

	// Work bottom-to-top, right-to-left.
	int srcwidth = Width / scalex;
	int srcheight = Height / scaley;

	const uint8_t *src = Pixels + (srcheight - 1) * Pitch;	// src points to the beginning of the last line
	uint8_t *dest = Pixels + Height * Pitch;				// dest points just past the end of the last line

	switch (BytesPerPixel)
	{
	case 1: Expand1(scalex, scaley, srcwidth, srcheight, src, dest); break;
	case 2: Expand2(scalex, scaley, srcwidth, srcheight, (const uint16_t *)src, (uint16_t *)dest); break;
	case 4: Expand4(scalex, scaley, srcwidth, srcheight, (const uint32_t *)src, (uint32_t *)dest); break;
	}
}

void ChunkyBitmap::Expand1(int scalex, int scaley, int srcwidth, int srcheight, const uint8_t *src, uint8_t *dest) noexcept
{
	for (int sy = srcheight; sy > 0; --sy, src -= Width)
	{
		int yy = scaley;
		const uint8_t *ysrc;

		// If expanding both horizontally and vertically, each source row only needs
		// to be expanded once because the vertical expansion can copy the already-
		// expanded line the rest of the way.
		if (scalex != 1)
		{ // Expand horizontally
			for (int sx = srcwidth - 1; sx >= 0; --sx)
				for (int xx = scalex; xx > 0; --xx)
					*--dest = src[sx];
			ysrc = dest;
			yy--;
		}
		else
		{ // Copy straight from source
			ysrc = src;
		}
		for (; yy > 0; --yy, dest -= Width)
			memcpy(dest - Width, ysrc, Pitch);
	}
}

void ChunkyBitmap::Expand2(int scalex, int scaley, int srcwidth, int srcheight, const uint16_t *src, uint16_t *dest) noexcept
{
	for (int sy = srcheight; sy > 0; --sy, src -= Width)
	{
		int yy = scaley;
		const uint16_t *ysrc;

		// If expanding both horizontally and vertically, each source row only needs
		// to be expanded once because the vertical expansion can copy the already-
		// expanded line the rest of the way.
		if (scalex != 1)
		{ // Expand horizontally
			for (int sx = srcwidth - 1; sx >= 0; --sx)
				for (int xx = scalex; xx > 0; --xx)
					*--dest = src[sx];
			ysrc = dest;
			yy--;
		}
		else
		{ // Copy straight from source
			ysrc = src;
		}
		for (; yy > 0; --yy, dest -= Width)
			memcpy(dest - Width, ysrc, Pitch);
	}
}

void ChunkyBitmap::Expand4(int scalex, int scaley, int srcwidth, int srcheight, const uint32_t *src, uint32_t *dest) noexcept
{
	for (int sy = srcheight; sy > 0; --sy, src -= Width)
	{
		int yy = scaley;
		const uint32_t *ysrc;

		// If expanding both horizontally and vertically, each source row only needs
		// to be expanded once because the vertical expansion can copy the already-
		// expanded line the rest of the way.
		if (scalex != 1)
		{ // Expand horizontally
			for (int sx = srcwidth - 1; sx >= 0; --sx)
				for (int xx = scalex; xx > 0; --xx)
					*--dest = src[sx];
			ysrc = dest;
			yy--;
		}
		else
		{ // Copy straight from source
			ysrc = src;
		}
		for (; yy > 0; --yy, dest -= Width)
			memcpy(dest - Width, ysrc, Pitch);
	}
}

// Convert OCS HAM6 to RGB
ChunkyBitmap ChunkyBitmap::HAM6toRGB(std::vector<ColorRegister> &pal)
{
	assert(pal.size() >= 16);
	assert(BytesPerPixel == 1);
	ChunkyBitmap out(Width, Height, 4);
	const uint8_t *src = Pixels;
	uint8_t *dest = out.Pixels;
	ColorRegister color = pal[0];

	for (int i = Width * Height; i > 0; --i, ++src, dest += 4)
	{
		uint8_t intensity = *src & 0x0F;
		intensity |= intensity << 4;
		switch (*src & 0xF0)
		{
		case 0x00: color = pal[*src]; break;
		case 0x10: color.blue = intensity; break;
		case 0x20: color.red = intensity; break;
		case 0x30: color.green = intensity; break;
		}
		dest[0] = color.red;
		dest[1] = color.green;
		dest[2] = color.blue;
		dest[3] = 0xFF;
	}
	return out;
}

// Convert AGA HAM8 to RGB
ChunkyBitmap ChunkyBitmap::HAM8toRGB(std::vector<ColorRegister> &pal)
{
	assert(pal.size() >= 64);
	assert(BytesPerPixel == 1);
	ChunkyBitmap out(Width, Height, 4);
	const uint8_t *src = Pixels;
	uint8_t *dest = out.Pixels;
	ColorRegister color = pal[0];

	for (int i = Width * Height; i > 0; --i, ++src, dest += 4)
	{
		uint8_t intensity = *src & 0x3F;
		intensity = (intensity << 2) | (intensity >> 4);
		switch (*src & 0xC0)
		{
		case 0x00: color = pal[*src]; break;
		case 0x40: color.blue = intensity; break;
		case 0x80: color.red = intensity; break;
		case 0xC0: color.green = intensity; break;
		}
		dest[0] = color.red;
		dest[1] = color.green;
		dest[2] = color.blue;
		dest[3] = 0xFF;
	}
	return out;
}

static int NearestColor(const ColorRegister *pal, int r, int g, int b, int first, int num)
{
	int bestcolor = first;
	int bestdist = INT_MAX;

	for (int color = first; color < num; color++)
	{
		int rmean = (r + pal[color].red) / 2;
		int x = r - pal[color].red;
		int y = g - pal[color].green;
		int z = b - pal[color].blue;
		//int dist = x * x + y * y + z * z;
		int dist = (512 + rmean) * x * x + 1024 * y * y + (767 - rmean) * z * z;
		if (dist < bestdist)
		{
			if (dist == 0)
				return color;

			bestdist = dist;
			bestcolor = color;
		}
	}
	return bestcolor;
}

ChunkyBitmap ChunkyBitmap::Quantize(const ColorRegister *pal, int npal)
{
	assert(BytesPerPixel == 4);
	ChunkyBitmap out(Width, Height);
	const uint8_t *src = Pixels;
	uint8_t *dest = out.Pixels;

	// Not optimal, but I just want something output for the moment.
	for (int i = Width * Height; i > 0; --i)
	{
		*dest++ = NearestColor(pal, src[0], src[1], src[2], 0, npal);
		src += 4;
	}
	return out;
}
