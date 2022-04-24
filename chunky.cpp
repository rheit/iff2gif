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
#include <array>
#include <algorithm>
#include "iff2gif.h"

#ifdef __linux__
#include <cstring>
#include <climits>
#include <cmath>
#endif

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

ChunkyBitmap::ChunkyBitmap(const ChunkyBitmap& o)
	: Width(o.Width), Height(o.Height), Pitch(o.Pitch),
	  BytesPerPixel(o.BytesPerPixel)
{
	Pixels = new uint8_t[Pitch * Height];
	memcpy(Pixels, o.Pixels, Pitch * Height);
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

bool ChunkyBitmap::operator==(ChunkyBitmap &o) noexcept
{
	if (&o == this) return true;
	return Width == o.Width
		&& Height == o.Height
		&& Pitch == o.Pitch
		&& BytesPerPixel == o.BytesPerPixel
		&& 0 == memcmp(Pixels, o.Pixels, Pitch * Height);
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
ChunkyBitmap ChunkyBitmap::HAM6toRGB(const Palette &pal) const
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
ChunkyBitmap ChunkyBitmap::HAM8toRGB(const Palette &pal) const
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

static const ChunkyBitmap::Diffuser
FloydSteinberg[] = {
	{ 28672, { {1, 0} } },								// 7/16
	{ 12288, { {-1, 1} } },								// 3/16
	{ 20480, { {0, 1} } },								// 5/16
	{  4096, { {1, 1} } },								// 1/16
	{ 0 } },

JarvisJudiceNinke[] = {
	{ 9557, { {1, 0}, {0, 1} } },						// 7/48
	{ 6826, { {2, 0}, {-1, 1}, {1, 1}, {0, 2} } },		// 5/48
	{ 4096, { {-2, 1}, {2, 1}, {-1, 2}, {1, 2} } },		// 3/48
	{ 1365, { {-2, 2}, {2, 2} } },						// 1/48
	{ 0 } },

Stucki[] = {
	{ 12483, { {1, 0}, {0, 1} } },						// 8/42
	{  6241, { {2, 0}, {-1, 1}, {1, 1}, {0, 2} } },		// 4/42
	{  3120, { {-2, 1}, {2, 1}, {-1, 2}, {1, 2} } },	// 2/42
	{  1560, { {-2, 2}, {2, 2 } } },					// 1/42
	{ 0 } },

Atkinson[] = {
	{ 8192, { {1, 0}, {2, 0}, {-1, 1}, {0, 1}, {1, 1}, {0, 2} } },	// 1/8
	{ 0 } },

Burkes[] = {
	{ 16384, { {1, 0}, {0, 1} } },						// 8/32
	{  8192, { {2, 0}, {-1, 1}, {1, 1} } },				// 4/32
	{  4096, { {-2, 1}, {2, 1} } },						// 2/32
	{ 0 } },

Sierra3[] = {
	{ 10240, {{1, 0}, {0, 1}} },						// 5/32
	{  8192, {{-1,1}, {1, 1}} },						// 4/32
	{  6144, {{2, 0}, {0, 2}} },						// 3/32
	{  4096, {{-2, 1}, {2, 1}, {-1, 2}, {1, 2}} },		// 2/32
	{ 0 } },

Sierra2[] = {
	{ 16384, {{1, 0}} },								// 4/16
	{ 12288, {{2, 0}, {0, 1}} },						// 3/16
	{  8192, {{-1, 1}, {1, 1}} },						// 2/16
	{  4096, {{-2, 1}, {2, 1}} },						// 1/16
	{ 0 } },

SierraLite[] = {
	{ 32768, {{1, 0}} },								// 2/4
	{ 16384, {{-1, 1}, {0, 1}} },						// 1/4
	{ 0 } }
;

static const ChunkyBitmap::Diffuser *const ErrorDiffusionKernels[] = {
	FloydSteinberg,
	JarvisJudiceNinke,
	Stucki,
	Burkes,
	Atkinson,
	Sierra3,
	Sierra2,
	SierraLite
};

class Palettizer
{
public:
	Palettizer(const ChunkyBitmap &bitmap, const Palette &pal) : Bitmap(bitmap), Pal(pal) { assert(bitmap.BytesPerPixel == 4); }
	virtual ~Palettizer() {}
	virtual void GetPixels(uint8_t *dest, int x, int y, int width) = 0;

protected:
	const ChunkyBitmap &Bitmap;
	const Palette &Pal;
};

class NoDitherPalettizer : public Palettizer
{
public:
	NoDitherPalettizer(const ChunkyBitmap &bitmap, const Palette &pal) : Palettizer(bitmap, pal) {}
	void GetPixels(uint8_t *dest, int x, int y, int width) override;
};

void NoDitherPalettizer::GetPixels(uint8_t *dest, int x, int y, int width)
{
	const uint8_t *src = Bitmap.Pixels + y * Bitmap.Pitch + x * 4;
	while (width--)
	{
		*dest++ = Pal.NearestColor(src[0], src[1], src[2]);
		src += 4;
	}
}

class ErrorDiffusionPalettizer : public Palettizer
{
public:
	ErrorDiffusionPalettizer(const ChunkyBitmap &bitmap, const Palette &pal, const ChunkyBitmap::Diffuser *kernel);
	void GetPixels(uint8_t *dest, int x, int y, int width) override;

protected:
	void ShiftError(int newy);

	const ChunkyBitmap::Diffuser *Kernel;

	// None of the error diffusion kernels need to keep track of more than 3
	// rows of error, so this is enough. Error is stored as 16.16 fixed point,
	// so the accumulated error can be applied to the output color with just
	// a bit shift and no division.
	std::vector<std::array<int, 3>> Error[3];

	// Row of image for first row of Error[]
	int ErrorY = 0;
};

ErrorDiffusionPalettizer::ErrorDiffusionPalettizer(const ChunkyBitmap &bitmap, const Palette &pal,
	const ChunkyBitmap::Diffuser *kernel)
	: Palettizer(bitmap, pal), Kernel(kernel)
{
	for (auto &arr : Error)
	{
		arr.resize(bitmap.Width);
	}
}

void ErrorDiffusionPalettizer::ShiftError(int newy)
{
	assert(newy > ErrorY && "Please don't go backward");
	int zeroloc;

	if (newy == ErrorY + 1)
	{ // Advance one row
		Error[0].swap(Error[1]);	// Move row 1 to row 0
		Error[1].swap(Error[2]);	// Move row 2 to row 1
		zeroloc = 2;				// Zero row 2
	}
	else if (newy == ErrorY + 2)
	{ // Advance two rows
		Error[0].swap(Error[2]);	// Move row 2 to row 0
		zeroloc = 1;				// Zero rows 1 and 2
	}
	else
	{ // Advancing three or more rows
		zeroloc = 0;				// Zero all rows
	}
	for (; zeroloc < 3; ++zeroloc)
	{
		std::fill(Error[zeroloc].begin(), Error[zeroloc].end(), std::array<int, 3>());
	}
	ErrorY = newy;
}

void ErrorDiffusionPalettizer::GetPixels(uint8_t *dest, int x, int y, int width)
{
	if (y != ErrorY)
	{
		ShiftError(y);
	}
	const uint8_t *src = Bitmap.Pixels + y * Bitmap.Pitch + x * 4;
	for (; width--; ++x, src += 4)
	{
		// Combine error with the pixel at this location and output
		// the palette entry that most closely matches it. The combined
		// color must be clamped to valid values, or you can end up with
		// bright sparkles in dark areas and vice-versa if the combined
		// color is "super-bright" or "super-dark". e.g. If error
		// diffusion made a black color "super-black", the best we can
		// actually output is black, so the difference between black and
		// the theoretical "super-black" we "wanted" could be diffused
		// out to produce grays specks in what should be a solid black
		// area if we don't clamp the "super-black" to a regular black.
		int r = std::clamp(src[0] + (Error[0][x][0] >> 16), 0, 255);
		int g = std::clamp(src[1] + (Error[0][x][1] >> 16), 0, 255);
		int b = std::clamp(src[2] + (Error[0][x][2] >> 16), 0, 255);
		int c = Pal.NearestColor(r, g, b);
		dest[x] = c;

		// Diffuse the difference between what we wanted and what we got.
		r -= Pal[c].red;
		g -= Pal[c].green;
		b -= Pal[c].blue;
		// For each weight...
		for (const ChunkyBitmap::Diffuser *desc = Kernel; desc->weight != 0; ++desc)
		{
			int rw = r * desc->weight;
			int gw = g * desc->weight;
			int bw = b * desc->weight;
			// ...apply that weight to one or more pixels.
			for (int j = 0; j < countof(desc->to) && desc->to[j].x | desc->to[j].y; ++j)
			{
				int xx = x + desc->to[j].x;
				if (xx >= 0 && xx < Bitmap.Width)
				{
					Error[desc->to[j].y][xx][0] += rw;
					Error[desc->to[j].y][xx][1] += gw;
					Error[desc->to[j].y][xx][2] += bw;
				}
			}
		}
	}
}

ChunkyBitmap ChunkyBitmap::RGBtoPalette(const Palette &pal, int dithermode) const
{
	ChunkyBitmap out(Width, Height);
	std::unique_ptr<Palettizer> palettizer;

	if (dithermode <= 0 || dithermode > countof(ErrorDiffusionKernels))
	{
		palettizer = std::make_unique<NoDitherPalettizer>(*this, pal);
	}
	else
	{
		palettizer = std::make_unique<ErrorDiffusionPalettizer>(*this, pal,
			ErrorDiffusionKernels[dithermode - 1]);
	}
	for (int y = 0; y < Height; ++y)
	{
		palettizer->GetPixels(out.Pixels + y * out.Pitch, 0, y, Width);
	}
	return out;
}
