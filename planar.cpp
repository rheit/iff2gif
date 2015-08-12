/* This file is part of iff2gif.
**
** Copyright 2015 - Randy Heit
**
** iff2gif is free software : you can redistribute it and / or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** Foobar is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with iff2gif. If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <assert.h>
#include <string.h>
#include "iff2gif.h"

PlanarBitmap::PlanarBitmap(int w, int h, int nPlanes)
{
	assert(nPlanes >= 0 && nPlanes < 32);

	int i;

	Width = w;
	Height = h;
	// Amiga bitplanes must be an even number of bytes wide
	Pitch = ((w + 15) / 16) * 2;
	NumPlanes = nPlanes;
	Palette = NULL;
	PaletteSize = 0;
	TransparentColor = -1;
	Interleave = 0;
	Delay = 0;
	Rate = 60;

	// We always allocate at least 8 planes for faster planar to chunky conversion.
	int realplanes = std::max(nPlanes, 8);
	PlaneData = new UBYTE[Pitch * Height * realplanes];
	memset(PlaneData, 0, Pitch * Height * realplanes);

	for (i = 0; i < nPlanes; ++i)
	{
		Planes[i] = PlaneData + (Pitch * Height) * i;
	}
	for (; i < 32; ++i)
	{
		Planes[i] = NULL;
	}
}

PlanarBitmap::PlanarBitmap(const PlanarBitmap &o)
{
	Width = o.Width;
	Height = o.Height;
	Pitch = o.Pitch;
	NumPlanes = o.NumPlanes;
	Palette = o.Palette;
	PaletteSize = o.PaletteSize;
	TransparentColor = o.TransparentColor;
	Interleave = o.Interleave;
	Delay = o.Delay;
	Rate = o.Rate;

	int realplanes = std::max(NumPlanes, 8);
	PlaneData = new UBYTE[Pitch * Height * realplanes];
	memcpy(PlaneData, o.PlaneData, Pitch * Height * realplanes);
	for (int i = 0; i < 32; ++i)
	{
		if (o.Planes[i] == NULL)
		{
			Planes[i] = NULL;
		}
		else
		{
			Planes[i] = PlaneData + (Pitch * Height) * i;
			memcpy(Planes[i], o.Planes[i], Pitch * Height);
		}
	}
}

PlanarBitmap::~PlanarBitmap()
{
	if (PlaneData != NULL)
	{
		delete[] PlaneData;
	}
}

void PlanarBitmap::FillBitplane(int plane, bool set)
{
	assert(plane >= 0 && plane < NumPlanes);
	memset(Planes[plane], -(UBYTE)set, Pitch * Height);
}

// Converts bitplanes to chunky pixels. The size of dest is selected based
// on the number of planes:
//	    0: do nothing
//    1-8: one byte
//	 9-16: two bytes
//  17-32: four bytes
void PlanarBitmap::ToChunky(void *dest)
{
	if (NumPlanes <= 0)
	{
		return;
	}
	else if (NumPlanes <= 8)
	{
#if 0
		UBYTE *out = (UBYTE *)dest;
		ULONG in = 0;
		for (int y = 0; y < Height; ++y)
		{
			for (int x = 0; x < Width; ++x)
			{
				int bit = 7 - (x & 7), byte = in + (x >> 3);
				UBYTE pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			in += Pitch;
		}
#else
		UBYTE *out = (UBYTE *)dest;
		ULONG in = 0;
		const int srcstep = Pitch * Height;
		for (int x, y = 0; y < Height; ++y)
		{
			// Do 8 pixels at a time
			for (x = 0; x < Width >> 3; ++x, out += 8)
			{
				rotate8x8(PlaneData + in + x, srcstep, out, 1);
			}
			// Do overflow
			ULONG byte = in + x;
			for (x <<= 3; x < Width; ++x)
			{
				const int bit = 7 - (x & 7);
				UBYTE pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			in += Pitch;
		}
#endif
	}
	else if (NumPlanes <= 16)
	{
		UWORD *out = (UWORD *)dest;
		ULONG in = 0;
		for (int y = 0; y < Height; ++y)
		{
			for (int x = 0; x < Width; ++x)
			{
				int bit = 7 - (x & 7), byte = in + (x >> 3);
				UWORD pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			in += Pitch;
		}
	}
	else
	{
		ULONG *out = (ULONG *)dest;
		ULONG in = 0;
		for (int y = 0; y < Height; ++y)
		{
			for (int x = 0; x < Width; ++x)
			{
				int bit = 7 - (x & 7), byte = in + (x >> 3);
				ULONG pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			in += Pitch;
		}
	}
}
