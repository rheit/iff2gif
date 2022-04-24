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
#include <stdio.h>

#ifdef __linux__
#include <cstring>
#include <climits>
#endif

#include "iff2gif.h"

// GIF restricts codes to 12 bits max
#define CODE_LIMIT (1 << 12)

class CodeStream
{
public:
	CodeStream(uint8_t mincodesize, std::vector<uint8_t> &codes);
	~CodeStream();
	void AddByte(uint8_t code);
	void WriteCode(uint16_t p);
	void Dump();

private:
	std::vector<uint8_t> &Codes;
	uint32_t Accum;
	uint16_t ClearCode;
	uint16_t EOICode;
	uint16_t NextCode;		// next code to assign
	int16_t Match;			// code of string matched so far
	uint8_t CodeSize;		// in bits
	uint8_t MinCodeSize;
	int8_t BitPos;
	uint8_t Chunk[256];		// first byte is length

	// The dictionary maps code strings to code words. Each possible pixel
	// value [0..size of palette) is automatically its own code word. A code
	// string consists of a code word plus an appended pixel value. The first
	// time a code string is encountered, it is inserted into the dictionary
	// and becomes another code word. Thus, it is enough to represent a code
	// string as a code word plus a single value appended to it. In this case,
	// the code string is represented as a 25-bit value arranged as:
	//
	//     2         1         0  \ bit
	// 4321098765432109876543210  / number
	//          ^^^^^^^^^^^^^^^^  code word
	//  ^^^^^^^^ value appended to code word
	// 1 <- indicates that this is a code string
	//
	// GIF limits the code word to 12 bits, so 16 bits is overkill, but it's
	// nice for viewing it as a hexadecimal number. There are 8 bits for the
	// appended value, since GIF works with at-most 8 bits per pixel. Bit 24
	// is always set in order to disambiguate between a lone code word and a
	// code string where the appended value is 0.

	typedef std::unordered_map<uint32_t, uint16_t> DictType;
	DictType Dict;

	void ResetDict();
	void DumpAccum(bool full);
};

void LZWCompress(std::vector<uint8_t> &vec, const ImageDescriptor &imd, const ChunkyBitmap &cbprev,
	const ChunkyBitmap &chunky, uint8_t mincodesize, int trans);

GIFWriter::GIFWriter(tstring filename, bool solo, int forcedrate,
	std::vector<std::pair<unsigned, unsigned>> &clips, int diffusion)
	: BaseFilename(filename), SoloMode(solo), ForcedFrameRate(forcedrate > 0),
	  DiffusionMode(diffusion), Clips(clips)
{
	if (forcedrate > 0)
	{
		FrameRate = forcedrate;
	}
	memset(&LSD, 0, sizeof(LSD));
	if (solo)
	{
		CheckForIndexSpot();
	}
	if (Clips.size() == 0)
	{
		Clips.push_back({ 1, UINT_MAX });
	}
}

GIFWriter::~GIFWriter()
{
	if (WriteQueue.Total() == 1)
	{
		// The header is not normally written until we reach the second frame of the
		// input. For a single frame image, we need to write it now.
		WriteHeader(false);
	}
	FinishFile();
}

bool GIFWriter::FinishFile()
{
	// Pretend success if no file is open.
	bool succ = true;
	if (File != nullptr)
	{
		// If the first frame had a delay, set it on the final frame
		// for looping purposes. (ANIM delays the start of this frame,
		// but GIF delays the start of the next frame.)
		auto oldframe = WriteQueue.MostRecent();
		if (oldframe != nullptr)
		{
			AddDelay(oldframe, FirstDelay);
		}
		// The 0x3B is a trailer byte to terminate the GIF.
		succ = WriteQueue.Flush() && fputc(0x3B, File) != EOF;
		if (succ)
		{
			fclose(File);
			WriteQueue.SetFile(nullptr);
			File = nullptr;
		}
		else
		{
			BadWrite();
		}
	}
	return succ;
}

void GIFWriter::BadWrite()
{
	_ftprintf(stderr, _T("Could not write to %s: %s\n"), Filename.c_str(), _tcserror(errno));
	fclose(File);
	File = nullptr;
	WriteQueue.SetFile(nullptr);
}

// When created in solo mode, check the output filename to see if it includes
// a placeholder for the frame index.
void GIFWriter::CheckForIndexSpot()
{
	// Find the extension (if there is one).
	auto stop = BaseFilename.find_last_of(_T('.'));
	// If . is the last character, it's not really an extension.
	SExtIndex = (stop == tstring::npos || stop == BaseFilename.length() - 1) ? -1 : (int)stop;
	int startidx = SExtIndex >= 0 ? SExtIndex : (int)BaseFilename.length();
	int idx = startidx;
	// Check for trailing 0s
	while (--idx >= 0 && BaseFilename[idx] == _T('0'))
	{
		// pass
	}
	SFrameIndex = idx + 1;
	SFrameLength = startidx - idx - 1;
}

// Generate a new filename to write to. In normal operation, this is just BaseFilename.
// In solo mode, this is a newly-created filename.
void GIFWriter::GenFilename()
{
	if (!SoloMode)
	{
		Filename = BaseFilename;
		return;
	}
	Filename = BaseFilename.substr(0, SFrameIndex);
	tstring index = to_tstring(FrameCount + 1);		// Use a 1-based index
	if (index.length() < SFrameLength)
	{
		Filename += tstring(SFrameLength - index.length(), _T('0'));
	}
	Filename += index;
	if (SExtIndex >= 0)
	{
		Filename += BaseFilename.substr((size_t)SExtIndex);
	}
}

// Returns the number of decimal digits needed to display num.
static int numdigits(int num)
{
	int ndig = 1;
	while (num > 9)
	{
		num /= 10;
		ndig++;
	}
	return ndig;
}

static Palette DumbPalette()
{
	// The so-called "web-safe" palette with some extra shades of gray
	std::vector<ColorRegister> pal;

	if (pal.empty())
	{
		// Colors
		for (int r = 0; r < 6; ++r)
			for (int g = 0; g < 6; ++g)
				for (int b = 0; b < 6; ++b)
					pal.emplace_back(r * 255 / 5, g * 255 / 5, b * 255 / 5);
		// Grays
		for (int g = 8; g < 256; g += 8)
			pal.emplace_back(g, g, g);
	}
	return pal;
}

// bitmap is used for basic metadata about the image, but the actual
// image data comes from chunky.
void GIFWriter::AddFrame(const PlanarBitmap *bitmap, ChunkyBitmap &&chunky)
{
	Palette palette = bitmap->Palette;
	int mincodesize = bitmap->NumPlanes;

	if (chunky.BytesPerPixel != 1)
	{
		//palette = DumbPalette();
		std::unique_ptr<Quantizer> quant{ QuantizerFactory[QUANTIZER_NeuQuant](256) };
		quant->AddPixels(chunky);
		palette = quant->GetPalette();
		palette = palette.Extend();
		chunky = chunky.RGBtoPalette(palette, DiffusionMode);
		mincodesize = palette.Bits();
	}

	if (FrameCount == 0)
	{ // Initialize some values from the initial frame.
		printf("%dx%dx%d\n", bitmap->Width, bitmap->Height, bitmap->NumPlanes);
		PageWidth = chunky.Width;
		PageHeight = chunky.Height;
		// GIF palettes must be a power of 2 in size. CMAP chunks have no such restriction.
		GlobalPal = palette.Extend();
		DetectBackgroundColor(bitmap, chunky);
		if (SFrameLength == 0)
		{ // Automatically decide what should be an adequate length for the frame number
		  // part of the filename in solo mode if we haven't already got a length for it.
			SFrameLength = numdigits(bitmap->NumFrames);
		}
		FirstDelay = bitmap->Delay;
	}
	if (bitmap->Rate > 0 && !ForcedFrameRate)
	{
		FrameRate = bitmap->Rate;
	}
	FrameCount++;
	// Only make the frame if it's in a desired clip range.
	if (!Clips.empty())
	{
		if (FrameCount >= Clips[0].first)
		{
			// In solo mode, always create a file. In normal mode, wait until
			// we get to the second frame, so we know if it's loopable or not.
			if (SoloMode || WriteQueue.Total() == 1)
			{
				WriteHeader(true);
			}
			MakeFrame(bitmap, std::move(chunky), palette, mincodesize);
		}
		if (FrameCount == Clips[0].second)
		{
			Clips.erase(begin(Clips));
			// If we exhausted every clip, make sure to write the final frames.
			// Normally, the final two would be dropped if we did the whole file,
			// since they duplicate the first two.
			if (Clips.empty())
			{
				WriteQueue.SetDropFrames(0);
			}
		}
	}
}

void GIFWriter::WriteHeader(bool loop)
{
	LogicalScreenDescriptor lsd = { LittleShort(PageWidth), LittleShort(PageHeight), 0, BkgColor, 0 };

	if (GlobalPal.Bits() > 0)
	{
		lsd.Flags = 0xF0 | (GlobalPal.Bits() - 1);
	}

	if (SoloMode)
	{
		loop = false;	// never loop in solo mode (because there's only one frame)
		if (File != nullptr && !FinishFile())
		{
			BadWrite();
			return;
		}
	}
	assert(File == nullptr);
	GenFilename();
	File = _tfopen(Filename.c_str(), _T("wb"));
	if (File == NULL)
	{
		_ftprintf(stderr, _T("Could not open %s: %s\n"), Filename.c_str(), _tcserror(errno));
		return;
	}
	WriteQueue.SetFile(File);
	if (fwrite("GIF89a", 6, 1, File) != 1 || fwrite(&lsd, 7, 1, File) != 1)
	{
		BadWrite();
		return;
	}
	// Write (or skip) palette
	if (lsd.Flags & 0x80)
	{
		assert(GlobalPal.size() == (size_t)1 << GlobalPal.Bits());
		if (fwrite(&GlobalPal[0], 3, GlobalPal.size(), File) != GlobalPal.size())
		{
			BadWrite();
			return;
		}
	}
	// Write (or skip) the looping extension
	if (loop)
	{
		if (fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00", 19, 1, File) != 1)
		{
			BadWrite();
			return;
		}
	}
}

void GIFWriter::MakeFrame(const PlanarBitmap *bitmap, ChunkyBitmap &&chunky, const Palette &palette, int mincodesize)
{
	GIFFrame newframe, *oldframe;
	bool palchanged;

	WriteQueue.SetDropFrames(SoloMode ? 0 : bitmap->Interleave);
	newframe.IMD.Width = chunky.Width;
	newframe.IMD.Height = chunky.Height;

	// Is there a transparent color?
	if (bitmap->TransparentColor >= 0)
	{
		newframe.GCE.Flags = 1;
		newframe.GCE.TransparentColor = bitmap->TransparentColor;
	}
	// Update properties on the preceding frame that couldn't be determined
	// until this frame.
	oldframe = WriteQueue.MostRecent();
	if (oldframe != nullptr)
	{
		oldframe->GCE.Flags |= SelectDisposal(bitmap, newframe.IMD, chunky) << 2;
		AddDelay(oldframe, bitmap->Delay);
	}
	// Check for a palette different from the one we recorded for the global color table.
	// Unlike ANIMs, where a CMAP chunk in one frame applies to that frame and all
	// subsequent frames until another CMAP, GIF's local color table applies only to
	// the frame where it appears.
	if (palette != GlobalPal)
	{
		newframe.LocalPalette = palette.Extend();
	}
	// If the palette has changed from the previous frame, we must redraw the entire frame,
	// because decoders probably won't repaint the old area with the new palette.
	palchanged = oldframe != nullptr && newframe.LocalPalette != oldframe->LocalPalette;

	// Identify the minimum rectangle that needs to be updated.
	if (!PrevFrame.IsEmpty() && !palchanged)
	{
		MinimumArea(PrevFrame, chunky, newframe.IMD);
	}
	// Replaces unchanged pixels with a transparent color, if there's room in the palette.
	int trans;
	bool temptrans = false;
	if (WriteQueue.Total() == 0 || PrevFrame.IsEmpty() || palchanged || (newframe.GCE.Flags & 0x1C0) == 0x80)
	{
		trans = -1;
	}
	else if (newframe.GCE.Flags & 1)
	{
		trans = newframe.GCE.TransparentColor;
	}
	else
	{
		trans = SelectTransparentColor(PrevFrame, chunky, newframe.IMD);
		if (trans >= 0)
		{
			newframe.GCE.Flags |= 1;
			newframe.GCE.TransparentColor = trans;
			temptrans = true;
		}
	}
	// Compressed the image data
	LZWCompress(newframe.LZW, newframe.IMD, PrevFrame, chunky, mincodesize, trans);
	// If we did transparent substitution, try again without. Sometimes it compresses
	// better if we don't do that.
	if (trans >= 0)
	{
		std::vector<uint8_t> try2;
		LZWCompress(try2, newframe.IMD, PrevFrame, chunky, mincodesize, -1);
		size_t l = newframe.LZW.size();
		size_t r = try2.size();
		if (try2.size() <= newframe.LZW.size())
		{
			newframe.LZW = std::move(try2);
			if (temptrans)
			{ // Undo the transparent color
				newframe.GCE.Flags &= 0xFE;
				newframe.GCE.TransparentColor = 0;
			}
		}
	}
	// Queue this frame for later writing, possibly flushing one frame to disk.
	if (!WriteQueue.Enqueue(std::move(newframe), bitmap))
	{
		BadWrite();
	}
	if (SoloMode)
	{
		chunky.Clear();
	}
	PrevFrame = std::move(chunky);
}

void GIFWriter::AddDelay(GIFFrame* oldframe, int delay)
{
	// GIF timing is in 1/100 sec. ANIM timing is in multiples of an FPS clock.
	// GIF delay is the delay until you show the next frame.
	// ANIM delay is the delay until you show this frame.
	// So ANIM delay needs to be moved one frame earlier for GIF delay and
	// scaled appropriately.
	if (delay != 0)
	{
		uint32_t tick = TotalTicks + delay;
		uint32_t lasttime = GIFTime;
		uint32_t nowtime = tick * 100 / FrameRate;
		int delay = nowtime - lasttime;
		oldframe->SetDelay(delay);
		TotalTicks = tick;
		GIFTime += delay;
	}
}

void GIFWriter::DetectBackgroundColor(const PlanarBitmap *bitmap, const ChunkyBitmap &chunky)
{
	// The GIF specification includes a background color. CompuServe probably actually
	// used this. In practice, modern viewers just make the background be transparent
	// and completely ignore the background color. So really, the background color is
	// either the same as the transparent color, or it doesn't matter what it is.

	// If there is a transparent color, let it be the background.
	if (bitmap->TransparentColor >= 0)
	{
		BkgColor = bitmap->TransparentColor;
		assert(PrevFrame.IsEmpty());
		PrevFrame = ChunkyBitmap(chunky, BkgColor);
	}
	// Else, whatever. It doesn't matter.
	else
	{
		BkgColor = 0;
	}
}

template<typename T>
void MinArea(const T *prev, const T *cur, ImageDescriptor &imd)
{
	int32_t start = -1;
	int32_t end = imd.Width * imd.Height;
	int32_t p;
	int top, bot, left, right, x;

	// Scan from beginning to find first changed pixel.
	while (++start < end)
	{
		if (prev[start] != cur[start])
			break;
	}
	if (start == end)
	{ // Nothing changed! Use a dummy 1x1 rectangle in case a GIF viewer would choke
	  // on no image data at all in a frame.
		imd.Width = 1;
		imd.Height = 1;
		return;
	}
	// Scan from end to find last changed pixel.
	while (--end > start)
	{
		if (prev[end] != cur[end])
			break;
	}
	// Now we know the top and bottom of the changed area, but not the left and right.
	top = start / imd.Width;
	bot = end / imd.Width;
	// Find left edge.
	for (x = 0; x < imd.Width - 1; ++x)
	{
		p = top * imd.Width + x;
		for (int y = top; y <= bot; ++y, p += imd.Width)
		{
			if (prev[p] != cur[p])
				goto gotleft;
		}
	}
gotleft:
	left = x;
	// Find right edge.
	for (x = imd.Width - 1; x > 0; --x)
	{
		p = top * imd.Width + x;
		for (int y = top; y <= bot; ++y, p += imd.Width)
		{
			if (prev[p] != cur[p])
				goto gotright;
		}
	}
gotright:
	right = x;

	imd.Left = left;
	imd.Top = top;
	imd.Width = right - left + 1;
	imd.Height = bot - top + 1;
}

void GIFWriter::MinimumArea(const ChunkyBitmap &prev, const ChunkyBitmap &cur, ImageDescriptor &imd)
{
	assert(prev.BytesPerPixel == cur.BytesPerPixel);
	if (prev.BytesPerPixel == 1)
	{
		MinArea<uint8_t>(prev.Pixels, cur.Pixels, imd);
	}
	else
	{
		assert(prev.BytesPerPixel == 4);
		MinArea<uint32_t>(reinterpret_cast<uint32_t *>(prev.Pixels), reinterpret_cast<uint32_t *>(cur.Pixels), imd);
	}
}

// Select the disposal method for this frame.
uint8_t GIFWriter::SelectDisposal(const PlanarBitmap *planar, const ImageDescriptor &imd, const ChunkyBitmap &chunky)
{
	// If there is no transparent color, then we can keep the old frame intact.
	if (planar->TransparentColor < 0 || PrevFrame.IsEmpty())
	{
		return 1;
	}
	// If no pixels are being changed to a transparent color, we can keep the old frame intact.
	// Otherwise, we must dispose it to the background color, since that's the only way to
	// set a pixel transparent after it's been rendered opaque.
	const uint8_t *src = PrevFrame.Pixels + imd.Left + imd.Top * PrevFrame.Pitch;
	const uint8_t *dest = chunky.Pixels + imd.Left + imd.Top * chunky.Pitch;
	const uint8_t trans = planar->TransparentColor;
	for (int y = 0; y < imd.Height; ++y)
	{
		for (int x = 0; x < imd.Width; ++x)
		{
			if (src[x] != trans && dest[x] == trans)
			{
				// Dispose the preceding frame.
				PrevFrame.SetSolidColor(planar->TransparentColor);
				return 2;
			}
		}
		src += planar->Width;
		dest += planar->Width;
	}
	return 1;
}

// Compares pixels in the changed region and returns a color that is not used in the destination.
// This can be used as a transparent color for this frame for better compression, since the
// underlying unchanged pixels can be collapsed into a run of a single color.
int GIFWriter::SelectTransparentColor(const ChunkyBitmap &cbprev, const ChunkyBitmap &cbnow, const ImageDescriptor &imd)
{
	uint8_t used[256 / 8] = { 0 };
	uint8_t c;

	const uint8_t *prev = cbprev.Pixels + imd.Left + imd.Top * cbprev.Pitch;
	const uint8_t *now = cbnow.Pixels + imd.Left + imd.Top * cbnow.Pitch;
	// Set a bit for every color used in the dest that changed from the preceding frame
	for (int y = 0; y < imd.Height; ++y)
	{
		for (int x = 0; x < imd.Width; ++x)
		{
			if (prev[x] != (c = now[x]))
			{
				used[c >> 3] |= 1 << (c & 7);
			}
		}
		prev += cbprev.Pitch;
		now += cbnow.Pitch;
	}
	// Return the first unused color found. Returns -1 if they were all used.
	for (int i = 0; i < 256 / 8; ++i)
	{
		if (used[i] != 255)
		{
			int bits = used[i], j;
			for (j = 0; bits & 1; ++j)
			{
				bits >>= 1;
			}
			// The color must be a part of the palette, if the palette has
			// fewer than 256 colors.
			int color = (i << 3) + j;
			return color < GlobalPal.size() ? color : -1;
		}
	}
	return -1;
}

void LZWCompress(std::vector<uint8_t> &vec, const ImageDescriptor &imd, const ChunkyBitmap &cbprev,
	const ChunkyBitmap &chunky, uint8_t mincodesize, int trans)
{
	if (mincodesize < 2)
	{
		mincodesize = 2;
	}
	vec.push_back(mincodesize);
	CodeStream codes(mincodesize, vec);
	const uint8_t *in = chunky.Pixels + imd.Left + imd.Top * chunky.Pitch;
	if (trans < 0)
	{
		for (int y = 0; y < imd.Height; ++y)
		{
			for (int x = 0; x < imd.Width; ++x)
			{
				codes.AddByte(in[x]);
			}
			in += chunky.Pitch;
		}
	}
	else
	{
		const uint8_t transcolor = trans;
		const uint8_t *prev = cbprev.Pixels + imd.Left + imd.Top * cbprev.Pitch;
		for (int y = 0; y < imd.Height; ++y)
		{
			for (int x = 0; x < imd.Width; ++x)
			{
				codes.AddByte(prev[x] != in[x] ? in[x] : transcolor);
			}
			in += chunky.Pitch;
			prev += cbprev.Pitch;
		}
	}
}

CodeStream::CodeStream(uint8_t mincodesize, std::vector<uint8_t> &codes)
	: Codes(codes)
{
	assert(mincodesize >= 2 && mincodesize <= 8);
	MinCodeSize = mincodesize;
	CodeSize = MinCodeSize + 1;
	ClearCode = 1 << mincodesize;
	EOICode = ClearCode + 1;
	BitPos = 0;
	Accum = 0;
	memset(Chunk, 0, sizeof(Chunk));
	WriteCode(ClearCode);
}

CodeStream::~CodeStream()
{
	// Finish output
	if (Match >= 0)
	{
		WriteCode(Match);
	}
	WriteCode(EOICode);
	DumpAccum(true);
	Dump();
	// Write block terminator
	Codes.push_back(0);
}

void CodeStream::Dump()
{
	if (Chunk[0] > 0)
	{
		Codes.insert(Codes.end(), Chunk, Chunk + Chunk[0] + 1);
		Chunk[0] = 0;
	}
}

void CodeStream::WriteCode(uint16_t code)
{
	Accum |= code << BitPos;
	BitPos += CodeSize;
	assert(Chunk[0] < 255);
	DumpAccum(false);
	if (code == ClearCode)
	{
		ResetDict();
	}
}

// If <full> is true, dump every accumulated bit.
// If <full> is false, only dump every complete accumulated byte.
void CodeStream::DumpAccum(bool full)
{
	int8_t stop = full ? 0 : 7;
	while (BitPos > stop)
	{
		assert(Chunk[0] < 255);
		Chunk[1 + Chunk[0]] = Accum & 0xFF;
		Accum >>= 8;
		BitPos -= 8;
		if (++Chunk[0] == 255)
		{
			Dump();
		}
	}
}

void CodeStream::AddByte(uint8_t p)
{
	assert(p < (1 << MinCodeSize) && "p must be within the palette");
	if (Match < 0)
	{ // Start a new run. We know p is always in the dictionary.
		Match = p;
	}
	else
	{ // Is Match..p in the dictionary?
		uint32_t str = Match | (p << 16) | (1 << 24);
		DictType::const_iterator got = Dict.find(str);
		if (got != Dict.end())
		{ // Yes, so continue matching it.
			Match = got->second;
		}
		else
		{ // No, so write out the matched code and add this new string to the dictionary.
			WriteCode(Match);
			Dict[str] = NextCode++;
			if (NextCode == CODE_LIMIT)
			{
				WriteCode(ClearCode);
			}
			else if (NextCode == (1 << CodeSize) + 1)
			{
				CodeSize++;
			}

			// Start a new match string on this byte.
			Match = p;
		}
	}
}

void CodeStream::ResetDict()
{
	CodeSize = MinCodeSize + 1;
	NextCode = EOICode + 1;
	Match = -1;
	Dict.clear();
	// Initialize the dictionary with the raw bytes that can be in the image.
	for (int i = (1 << MinCodeSize) - 1; i >= 0; --i)
	{
		Dict[i] = i;
	}
}

GIFFrame::GIFFrame()
{
	GCE.ExtensionIntroducer = 0x21;
	GCE.GraphicControlLabel = 0xF9;
	GCE.BlockSize = 4;
	GCE.Flags = 0;
	GCE.DelayTime = 0;
	GCE.TransparentColor = 0;
	GCE.BlockTerminator = 0;

	IMD.Left = 0;
	IMD.Top = 0;
	IMD.Width = 0;
	IMD.Height = 0;
	IMD.Flags = 0;
}

GIFFrame::GIFFrame(GIFFrame&& o) noexcept
{
	*this = std::move(o);
}

GIFFrame &GIFFrame::operator= (const GIFFrame &o)
{
	GCE = o.GCE;
	IMD = o.IMD;
	LZW = o.LZW;
	LocalPalette = o.LocalPalette;
	return *this;
}

GIFFrame &GIFFrame::operator= (GIFFrame &&o) noexcept
{
	GCE = o.GCE;
	IMD = o.IMD;
	LZW = std::move(o.LZW);
	LocalPalette = std::move(o.LocalPalette);
	return *this;
}

bool GIFFrame::Write(FILE *file)
{
	if (file != NULL)
	{
		// Write Graphic Control Extension, if needed
		if (GCE.Flags != 0 || GCE.DelayTime != 0)
		{
			if (fwrite(&GCE, 8, 1, file) != 1)
			{
				return false;
			}
		}
		int localpalbits = LocalPalette.Bits();
		if (localpalbits > 0)
		{
			IMD.Flags = 0x80 | (localpalbits - 1);	// Set Local Color Table Flag
		}
		// Write the image descriptor
		if (fputc(0x2C, file) /* Identify the Image Separator */ == EOF ||
			fwrite(&IMD, 9, 1, file) != 1)
		{
			return false;
		}
		// Write local color table
		if (localpalbits > 0 && fwrite(&LocalPalette[0], 3, LocalPalette.size(), file) != LocalPalette.size())
		{
			return false;
		}
		// Write the compressed image data
		if (fwrite(&LZW[0], 1, LZW.size(), file) != LZW.size())
		{
			return false;
		}
		return true;
	}
	// Pretend success if no file open
	return true;
}

GIFFrameQueue::GIFFrameQueue()
{
	File = NULL;
	FinalFramesToDrop = 0;
}

GIFFrameQueue::~GIFFrameQueue()
{
	Flush();
}

bool GIFFrameQueue::Flush()
{
	bool wrote = true;

	if (Queue.empty()) return true;

	// Check that the last X frames match the first X frames. If not, then ignore FinalFramesToDrop
	if (FinalFramesToDrop)
	{
		assert(FinalFramesToDrop <= Queue.size());
		for (int i = 0; i < FinalFramesToDrop; ++i)
		{
			if (FirstFrames[i] != Queue[Queue.size() - FinalFramesToDrop + i].second)
			{
				FinalFramesToDrop = 0;
				break;
			}
		}
	}

	while (Queue.size() > FinalFramesToDrop)
	{
		if (!Shift())
		{
			wrote = false;
			break;
		}
	}
	Queue.clear();
	return wrote;
}

bool GIFFrameQueue::Enqueue(GIFFrame &&frame, const PlanarBitmap *source_bitmap)
{
	bool wrote = true;
	if (Queue.size() >= MAX_QUEUE_SIZE)
	{
		wrote = Shift();
	}
	Queue.push_back(std::make_pair(std::move(frame), *source_bitmap));
	if (FirstFrames.size() < MAX_QUEUE_SIZE)
	{
		FirstFrames.push_back(*source_bitmap);
	}
	TotalQueued++;
	return wrote;
}

// Write out one frame and shift the others left.
bool GIFFrameQueue::Shift()
{
	bool wrote = true;
	if (!Queue.empty())
	{
		wrote = Queue.front().first.Write(File);
		Queue.erase(Queue.begin());
	}
	return wrote;
}
