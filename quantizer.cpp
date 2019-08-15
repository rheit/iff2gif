/* This file is part of iff2gif.
**
** Copyright 2019 - Marisa Heit
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

extern Quantizer *NewMedianCut(int maxcolors);
extern Quantizer *NewNeuQuant(int maxcolors);

Quantizer *(*const QuantizerFactory[])(int) = {
	NewMedianCut,
	NewNeuQuant,
};

static_assert(countof(QuantizerFactory) == NUM_QUANTIZERS, "QuantizerFactory must have NUM_QUANTIZERS entries");

Quantizer::~Quantizer()
{
}

void Quantizer::AddPixels(const ChunkyBitmap &bitmap)
{
	assert(bitmap.BytesPerPixel == 4);
	AddPixels(bitmap.Pixels, bitmap.Width * bitmap.Height);
}

Palette Histogram::ToPalette() const
{
	std::vector<ColorRegister> pal(Histo.size());
	for (size_t i = 0; i < Histo.size(); ++i)
	{
		pal[i] = ColorRegister(Histo[i]);
	}
	return pal;
}

// Count all the unique colors in an image and optionally computes the 3D bounding box for those colors.
void Histogram::AddPixels(const uint8_t *src, size_t numpixels, uint8_t mins[3], uint8_t maxs[3])
{
	for (size_t j = numpixels; j > 0; --j, src += 4)
	{
		uint32_t color = *reinterpret_cast<const uint32_t *>(src);
		auto it = ColorToHisto.find(color);
		if (it != ColorToHisto.end())
		{
			Histo[it->second].Count++;
		}
		else
		{
			ColorToHisto[color] = Histo.size();
			Histo.emplace_back(src[0], src[1], src[2]);
		}
		if (mins && maxs)
		{
			for (int i = 0; i < 3; ++i)
			{
				if (src[i] < mins[i]) mins[i] = src[i];
				if (src[i] > maxs[i]) maxs[i] = src[i];
			}
		}
	}
}
