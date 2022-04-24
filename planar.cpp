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

#include <algorithm>
#include <assert.h>
#include <string.h>
#include "iff2gif.h"

PlanarBitmap::PlanarBitmap(int w, int h, int nPlanes)
{
	assert(nPlanes > 0 && nPlanes <= 32);

	int i;

	Width = w;
	Height = h;
	// Amiga bitplanes must be an even number of bytes wide
	Pitch = ((w + 15) / 16) * 2;
	NumPlanes = nPlanes;

	// We always allocate at least 8 planes for faster planar to chunky conversion.
	int realplanes = std::max(nPlanes, 8);
	PlaneData = new uint8_t[Pitch * Height * realplanes];
	memset(PlaneData, 0, Pitch * Height * realplanes);

	for (i = 0; i < nPlanes; ++i)
	{
		Planes[i] = PlaneData + (Pitch * Height) * i;
	}
	for (; i < 32; ++i)
	{
		Planes[i] = nullptr;
	}
}

PlanarBitmap::PlanarBitmap(const PlanarBitmap &o)
{
	Width = o.Width;
	Height = o.Height;
	Pitch = o.Pitch;
	NumPlanes = o.NumPlanes;
	Palette = o.Palette;
	TransparentColor = o.TransparentColor;
	Interleave = o.Interleave;
	Delay = o.Delay;
	Rate = o.Rate;
	ModeID = o.ModeID;

	int realplanes = std::max(NumPlanes, 8);
	PlaneData = new uint8_t[Pitch * Height * realplanes];
	memcpy(PlaneData, o.PlaneData, Pitch * Height * realplanes);
	for (int i = 0; i < 32; ++i)
	{
		if (o.Planes[i] == nullptr)
		{
			Planes[i] = nullptr;
		}
		else
		{
			Planes[i] = PlaneData + (Pitch * Height) * i;
			memcpy(Planes[i], o.Planes[i], Pitch * Height);
		}
	}
}

PlanarBitmap::PlanarBitmap(PlanarBitmap &&o) noexcept
{
	Width = o.Width;
	Height = o.Height;
	Pitch = o.Pitch;
	NumPlanes = o.NumPlanes;
	Palette = std::move(o.Palette);
	TransparentColor = o.TransparentColor;
	Interleave = o.Interleave;
	Delay = o.Delay;
	Rate = o.Rate;
	ModeID = o.ModeID;
	PlaneData = o.PlaneData;
	memcpy(Planes, o.Planes, sizeof(Planes));

	o.PlaneData = nullptr;
	o.Width = 0;
	o.Height = 0;
	o.NumPlanes = 0;
}

PlanarBitmap::~PlanarBitmap()
{
	if (PlaneData != nullptr)
	{
		delete[] PlaneData;
	}
}

PlanarBitmap &PlanarBitmap::operator=(PlanarBitmap &&o) noexcept
{
	if (PlaneData != nullptr)
	{
		delete[] PlaneData;
	}
	Width = o.Width;
	Height = o.Height;
	Pitch = o.Pitch;
	NumPlanes = o.NumPlanes;
	Palette = std::move(o.Palette);
	TransparentColor = o.TransparentColor;
	Interleave = o.Interleave;
	Delay = o.Delay;
	Rate = o.Rate;
	ModeID = o.ModeID;
	PlaneData = o.PlaneData;
	memcpy(Planes, o.Planes, sizeof(Planes));

	o.PlaneData = nullptr;
	o.Width = 0;
	o.Height = 0;
	o.NumPlanes = 0;

	return *this;
}

bool PlanarBitmap::operator==(const PlanarBitmap &o) noexcept
{
	if (this == &o) return true;
	return Width == o.Width
		&& Height == o.Height
		&& Pitch == o.Pitch
		&& NumPlanes == o.NumPlanes
		&& Palette == o.Palette
		&& ModeID == o.ModeID

// Don't consider non-image data when determining equality
//		&& TransparentColor == o.TransparentColor
//		&& Interleave == o.Interleave
//		&& Delay == o.Delay
//		&& Rate == o.Rate

		&& 0 == memcmp(PlaneData, o.PlaneData, Pitch * Height * NumPlanes);
}

void PlanarBitmap::FillBitplane(int plane, bool set)
{
	assert(plane >= 0 && plane < NumPlanes);
	memset(Planes[plane], -(uint8_t)set, Pitch * Height);
}

// Converts bitplanes to chunky pixels. The size of dest is selected based
// on the number of planes:
//	    0: do nothing
//    1-8: one byte
//	 9-16: two bytes
//  17-32: four bytes
void PlanarBitmap::ToChunky(void *dest, int destextrawidth) const
{
	if (NumPlanes <= 0)
	{
		return;
	}
	else if (NumPlanes <= 8)
	{
		uint8_t *out = (uint8_t *)dest;
		uint32_t in = 0;
		const int srcstep = Pitch * Height;
		for (int x, y = 0; y < Height; ++y)
		{
			// Do 8 pixels at a time
			for (x = 0; x < Width >> 3; ++x, out += 8)
			{
				rotate8x8(PlaneData + in + x, srcstep, out, 1);
			}
			// Do overflow
			uint32_t byte = in + x;
			for (x <<= 3; x < Width; ++x)
			{
				const int bit = 7 - (x & 7);
				uint8_t pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			out += destextrawidth;
			in += Pitch;
		}
	}
	else if (NumPlanes <= 16)
	{
		uint16_t *out = (uint16_t *)dest;
		uint32_t in = 0;
		for (int y = 0; y < Height; ++y)
		{
			for (int x = 0; x < Width; ++x)
			{
				int bit = 7 - (x & 7), byte = in + (x >> 3);
				uint16_t pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				*out++ = pixel;
			}
			out += destextrawidth;
			in += Pitch;
		}
	}
	else
	{
		uint8_t *out = (uint8_t *)dest;
		uint32_t in = 0;
		const int srcstep = Pitch * Height;
		for (int x, y = 0; y < Height; ++y)
		{
			// Do 8 pixels at a time
			for (x = 0; x < Width / 8; ++x, out += 8*4)
			{
				rotate8x8(PlaneData + in + x, srcstep, out, 4);							// Red
				rotate8x8(Planes[8] + in + x, srcstep, out + 1, 4);						// Green
				rotate8x8(Planes[16] + in + x, srcstep, out + 2, 4);					// Blue
				if (Planes[24])															// Alpha
				{
					rotate8x8(Planes[24] + in + x, srcstep, out + 3, 4);
				}
				else
				{ // Set alpha to opaque for images that don't have an alpha channel.
				  // This is completely ignored by iff2gif at the moment.
					for (int z = 0; z < 8; ++z)
						out[3 + z * 4] = 0xFF;
				}
			}
			// Do overflow
			uint32_t byte = in + x;
			for (x *= 8; x < Width; ++x)
			{
				const int bit = 7 - (x & 7);
				uint32_t pixel = 0;
				for (int i = NumPlanes - 1; i >= 0; --i)
				{
					pixel = (pixel << 1) | ((Planes[i][byte] >> bit) & 1);
				}
				if (NumPlanes < 32) pixel |= 0xFF000000;	// solid alpha if not 32-bit
				out[0] = pixel & 0xFF;			// Red
				out[1] = (pixel >> 8) & 0xFF;	// Green
				out[2] = (pixel >> 16) & 0xFF;	// Blue
				out[3] = (pixel >> 24) & 0xFF;	// Alpha
				out += 4;
			}
			out += destextrawidth * 4;
			in += Pitch;
		}
	}
}
