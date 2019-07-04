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

#include "iff2gif.h"

ChunkyBitmap::ChunkyBitmap(const PlanarBitmap &planar, int scalex, int scaley)
	: Width(planar.Width * scalex), Height(planar.Height * scaley)
{
	int bytesperpixel = planar.NumPlanes <= 8 ? 1 : planar.NumPlanes <= 16 ? 2 : 4;
	Pitch = Width * bytesperpixel;
	Pixels = new uint8_t[Pitch * Height];
	planar.ToChunky(Pixels, Width - planar.Width);
}

// Creates a new chunky bitmap with the same dimensions as o, but filled with fillcolor.
ChunkyBitmap::ChunkyBitmap(const ChunkyBitmap &o, int fillcolor)
	: Width(o.Width), Height(o.Height), Pitch(o.Pitch)
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
	: Width(o.Width), Height(o.Height), Pitch(o.Pitch), Pixels(o.Pixels)
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
}

void ChunkyBitmap::SetSolidColor(int color) noexcept
{
	if (Pixels != nullptr)
	{
		memset(Pixels, color, Pitch * Height);
	}
}
