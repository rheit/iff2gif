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
#include <climits>

// Sets NumBits according to the size of the palette.
void Palette::CalcBits()
{
	int bits = 0;
	while ((size_t)1 << bits < Pal.size())
		bits++;
	NumBits = bits;
}

// Returns a version of the palette extended to the next power of 2.
Palette Palette::Extend() const
{
	if (empty())
	{
		return {};
	}

	// What's the closest power of 2 the palette fits in?
	uint8_t p = 1;
	size_t numdest = 2, i;
	while (numdest < Pal.size() && p < 8)
		++p, numdest *= 2;

	std::vector<ColorRegister> dest(numdest);
	// The source could potentially have more colors than we need, but also
	// might not have enough.
	for (i = 0; i < std::min(Pal.size(), numdest); ++i)
	{
		dest[i] = Pal[i];
	}
	// Set extras to grayscale
	for (; i < numdest; ++i)
	{
		dest[i].blue = dest[i].green = dest[i].red = uint8_t((i * 255) >> p);
	}

	return Palette(std::move(dest), p);
}

// "Fix" the OCS palette by duplicating the high nibble into the low nibble.
void Palette::FixOCS()
{
	for (ColorRegister &reg : Pal)
	{
		reg.red |= reg.red >> 4;
		reg.green |= reg.green >> 4;
		reg.blue |= reg.blue >> 4;
	}
}

// In EHB mode, the palette has 64 entries, but the second 32 are implied
// as half intensity versions of the first 64.
void Palette::MakeEHB()
{
	if (Pal.empty())
	{ // What palette?
		return;
	}
	Pal.reserve(64);
	for (int i = 0; i < 32; ++i)
	{
		Pal[32 + i].red = Pal[i].red >> 1;
		Pal[32 + i].green = Pal[i].green >> 1;
		Pal[32 + i].blue = Pal[i].blue >> 1;
	}
}

int Palette::NearestColor(int r, int g, int b) const
{
	int bestcolor = 0;
	int bestdist = INT_MAX;

	for (int color = 0; color < Pal.size(); color++)
	{
		int rmean = (r + Pal[color].red) / 2;
		int x = r - Pal[color].red;
		int y = g - Pal[color].green;
		int z = b - Pal[color].blue;
		//int dist = x * x + y * y + z * z;
		// Thiadmer Riemersma's color distance equation from
		// https://www.compuphase.com/cmetric.htm
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
