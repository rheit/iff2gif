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
#include <algorithm>
#include <vector>
#include "iff2gif.h"

// This file implements the Modified Median Cut Quantization algorithm
// as described in "Color quantization using modified median cut" by
// Dan S. Bloomberg.

struct MCBin
{
	uint8_t Mins[3];		// Minimum Red, Green, and Blue
	uint8_t Maxs[3];		// Maximum Red, Green, and Blue
	int8_t SortDim;			// Which component this bin is sorted by, <0 for none
	uint32_t Count;			// Number of pixels represented in this bin
	uint32_t Begin;			// First histogram entry in this bin
	uint32_t End;			// One past the last histogram entry in this bin

	// Return the length of dimension i (0, 1, or 2)
	int Dim(int i) const;

	// Return which dimension is longest (0, 1, or 2)
	int LongestDim() const;

	// Sorts the histogram entries for this bin along dimension dim
	void Sort(Histogram &histo, int dim);

	// Splits this bin along dimension splitdim at position splitpt.
	// Returns a new bin split off from this one.
	MCBin Split(const Histogram &histo, int splitdim, int splitpt);

	// Returns true if this bin can be split further.
	bool CanSplit() const;
};

struct MCQueueComparator
{
	bool PopOnly;
	std::vector<MCBin> &Bins;

	MCQueueComparator(std::vector<MCBin> &bins) : PopOnly(true), Bins(bins) {}
	MCQueueComparator &operator=(const MCQueueComparator &o)
	{ // This is only used when clearing the priority queue. It will not copy
	  // the Bins reference. (Is that even possible?)
		PopOnly = o.PopOnly;
		return *this;
	}
	bool operator()(const uint32_t &a, const uint32_t &b) const
	{
		if (PopOnly)
		{
			return Bins[a].Count < Bins[b].Count;
		}
		size_t vola = 1, volb = 1;
		for (int i = 0; i < 3; ++i)
		{
			vola *= Bins[a].Dim(i);
			volb *= Bins[b].Dim(i);
		}
		return Bins[a].Count * vola < Bins[b].Count * volb;
	}
};

struct MCSplitComparator
{
	int Dim;

	MCSplitComparator(int dim) : Dim(dim) {}
	bool operator()(const Histogram::HistEntry &a, int pt)
	{
		return a.Component[Dim] < pt;
	}
};

class MedianCut : public Quantizer
{
public:
	MedianCut(int maxcolors);
	void AddPixels(const uint8_t *rgb, size_t count) override;
	Palette GetPalette() override;

private:
	Palette PaletteFromMCBins(const Histogram &histo, const std::vector<MCBin> &bins);
	Palette CalcPalette();

	void CheckBounds(int binnum, const MCBin &bin) const;

	std::vector<MCBin> Bins;
	Histogram Histo;
	int MaxColors;
};

MedianCut::MedianCut(int maxcolors)
	: Bins(1), MaxColors(maxcolors)
{
	// If dithering, the starting bin should be the entire color space.
	// If not dithering, it could be constrained to just the bounding box
	// around the colors actually used by swapping the 255 and 0 in this
	// initialization and letting AddToHistogram resize the box.
	for (unsigned i = 0; i < 3; ++i)
	{
		Bins[0].Mins[i] = 0;
		Bins[0].Maxs[i] = 255;
	}
}

void MedianCut::AddPixels(const uint8_t *rgb, size_t count)
{
	Histo.AddPixels(rgb, count, Bins[0].Mins, Bins[0].Maxs);
	Bins[0].Count += (uint32_t)count;
}

Palette MedianCut::GetPalette()
{
	if (Histo.size() <= MaxColors)
	{ // The image doesn't contain any more colors than we want,
	  // so there's no need to spend time cutting it.
		return Histo.ToPalette();
	}
	return CalcPalette();
}

void MedianCut::CheckBounds(int binnum, const MCBin &bin) const
{
#ifdef _DEBUG
	int lo[3] = { INT_MAX, INT_MAX, INT_MAX };
	int hi[3] = { INT_MIN, INT_MIN, INT_MIN };

	printf("bin %4d, cnt %6u: ", binnum, bin.Count);
	if (bin.Count)
	{
		for (unsigned i = bin.Begin; i < bin.End; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				if (Histo[i].Component[j] < lo[j]) lo[j] = Histo[i].Component[j];
				if (Histo[i].Component[j] > hi[j]) hi[j] = Histo[i].Component[j];
			}
		}
		for (int i = 0; i < 3; ++i)
			printf("(%d [%3d - %3d] {%3d - %3d}) ", i, bin.Mins[i], bin.Maxs[i], lo[i], hi[i]);
	}
	else
	{
		for (int i = 0; i < 3; ++i)
			printf("(%d [%3d - %3d]            ) ", i, bin.Mins[i], bin.Maxs[i]);
	}
	printf("\n");
#endif
}

Palette MedianCut::CalcPalette()
{
	unsigned i;
	MCQueueComparator comparator(Bins);
	std::priority_queue<uint32_t, std::vector<uint32_t>, MCQueueComparator> queue(comparator);

	// After dividing into reprio_at bins, further bins splits will
	// consider volume as well as population count.
	int reprio_at = (int)(MaxColors * 0.75);

	Bins[0].Begin = 0;
	Bins[0].End = (uint32_t)Histo.size();
	Bins[0].SortDim = -1;
	queue.push(0);

	while (Bins.size() < MaxColors && !queue.empty())
	{
		uint32_t binnum = queue.top();
		queue.pop();
		MCBin &bin{ Bins[binnum] };
		int splitdim = bin.LongestDim();

		// Split halfway between the median and the side furthest from it.
		bin.Sort(Histo, splitdim);
		// Locate the median based on population count. This is not exactly halfway
		// between begin and end, because each histogram entry can represent more
		// than one pixel.
		int mediancount = 0, medianstop = bin.Count / 2;
		for (i = bin.Begin; mediancount < medianstop && i < bin.End; ++i)
		{
			mediancount += Histo[i].Count;
		}
		int median = Histo[i - 1].Component[splitdim] + 1;
		int splitpt = median - bin.Mins[splitdim] > bin.Maxs[splitdim] - median
			? (median + bin.Mins[splitdim]) / 2
			: (median + bin.Maxs[splitdim]) / 2;
		if (splitpt == bin.Mins[splitdim])
			splitpt++;
		printf("Split bin %d (pop %u) @ %d on dim %d\n", binnum, bin.Count, splitpt, splitdim);
		uint32_t newbin = (uint32_t)Bins.size();
		Bins.emplace_back(bin.Split(Histo, splitdim, splitpt));
		CheckBounds(binnum, Bins[binnum]);
		CheckBounds(newbin, Bins[newbin]);

		if (Bins.size() != reprio_at)
		{ // Requeue this bin and the one split off from it, but only if they can be further split.
			if (Bins[binnum].CanSplit()) queue.push(binnum);
			if (Bins[newbin].CanSplit()) queue.push(newbin);
		}
		else
		{ // Requeue everything, now taking volume into account as well as population.
			comparator.PopOnly = false;
			std::priority_queue<uint32_t, std::vector<uint32_t>, MCQueueComparator> emptyq(comparator);
			queue = emptyq;
			for (uint32_t i = 0; i < Bins.size(); ++i)
				if (Bins[i].CanSplit())
					queue.push(i);
		}
	}
	return PaletteFromMCBins(Histo, Bins);
}

int MCBin::Dim(int i) const
{
	return Maxs[i] - Mins[i] + 1;
}

int MCBin::LongestDim() const
{
	int topdim = -1;
	int topi = -1;
	for (int i = 0; i < 3; ++i)
	{
		int d = Dim(i);
		if (d > topdim)
		{
			topdim = d;
			topi = i;
		}
	}
	return topi;
}

bool MCBin::CanSplit() const
{
	// If this bin is empty, it cannot be split.
	if (Count < 2) return false;
	// If this bin is a point, it cannot be split.
	int i;
	for (i = 0; i < 3; ++i)
		if (Dim(i) > 1)
			break;
	return i < 3;
}

void MCBin::Sort(Histogram &histo, int dim)
{
	if (SortDim != dim)
	{
		std::sort(&histo[Begin], &histo[End - 1] + 1,
			[dim](const Histogram::HistEntry &a, const Histogram::HistEntry &b)
			{ return a.Component[dim] < b.Component[dim]; });
		SortDim = dim;
	}
	assert(Mins[dim] <= histo[Begin].Component[dim]);
	assert(Maxs[dim] >= histo[End - 1].Component[dim]);
}

MCBin MCBin::Split(const Histogram &histo, int splitdim, int splitpt)
{
	assert(Mins[splitdim] < splitpt && splitpt <= Maxs[splitdim]);

	MCBin newbin;
	unsigned i;

	for (i = 0; i < 3; ++i)
	{
		newbin.Mins[i] = Mins[i];
		newbin.Maxs[i] = Maxs[i];
	}
	newbin.Mins[splitdim] = splitpt;
	this->Maxs[splitdim] = splitpt - 1;
	assert(newbin.Mins[splitdim] <= newbin.Maxs[splitdim]);
	assert(this->Mins[splitdim] <= this->Maxs[splitdim]);
	newbin.SortDim = SortDim;
	newbin.End = End;

	// Check if the split creates a bin with no pixels.
	if (splitpt <= histo[Begin].Component[splitdim])
	{
		End = Begin;
		newbin.Begin = Begin;
		newbin.Count = Count;
		Count = 0;
		return newbin;
	}
	if (splitpt > histo[End - 1].Component[splitdim])
	{
		newbin.Begin = newbin.End;
		newbin.Count = 0;
		return newbin;
	}

	// Find the beginning histogram entry for the new bin.
	MCSplitComparator comparator(splitdim);
	auto it = std::lower_bound(&histo[Begin], &histo[End - 1] + 1, splitpt, comparator);
	newbin.Begin = this->End = (uint32_t)(it - &histo[0]);
	assert(this->Begin < this->End);
	assert(newbin.Begin < newbin.End);

	// Figure out new population counts. Count the number of pixels in the bin with
	// fewer histogram entries and subtract that from the larger bin.
	if (End - Begin > newbin.End - newbin.Begin)
	{ // The new bin has fewer entries to count.
		uint32_t count = 0;
		for (i = newbin.Begin; i < newbin.End; ++i)
		{
			count += histo[i].Count;
		}
		this->Count -= count;
		newbin.Count = count;
	}
	else
	{ // This bin has fewer entries to count than the new one.
		uint32_t count = 0;
		for (i = Begin; i < End; ++i)
		{
			count += histo[i].Count;
		}
		newbin.Count = this->Count - count;
		this->Count = count;
	}
	return newbin;
}

Palette MedianCut::PaletteFromMCBins(const Histogram &histo, const std::vector<MCBin> &bins)
{
	std::vector<ColorRegister> pal(bins.size());
	for (size_t i = 0; i < pal.size(); ++i)
	{
		if (bins[i].Count == 0)
		{ // If the bin has no pixels, return the color at the center of its volume.
			pal[i] = ColorRegister(
				(bins[i].Mins[0] + bins[i].Maxs[0] + 1) / 2,
				(bins[i].Mins[1] + bins[i].Maxs[1] + 1) / 2,
				(bins[i].Mins[2] + bins[i].Maxs[2] + 1) / 2);
		}
		else
		{ // Otherwise, average the pixels it contains
			size_t tot[3] = { 0, 0, 0 };
			for (size_t j = bins[i].Begin; j < bins[i].End; ++j)
				for (int k = 0; k < 3; ++k)
					tot[k] += histo[j].Component[k] * histo[j].Count;
			pal[i] = ColorRegister(
				int(tot[0] / bins[i].Count),
				int(tot[1] / bins[i].Count),
				int(tot[2] / bins[i].Count));
		}
	}
	return pal;
}

Quantizer *NewMedianCut(int maxcolors)
{
	return new MedianCut(maxcolors);
}
