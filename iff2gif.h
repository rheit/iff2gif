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

#include <vector>
#include <queue>
#include <iostream>
#include <memory>
#include <unordered_map>
#include "types.h"
#include "iff.h"

struct ColorRegister {				/* size = 3 bytes			*/
	uint8_t red, green, blue;		/* color intensities 0..255 */
	ColorRegister(const ColorRegister &o) : red(o.red), green(o.green), blue(o.blue) {}
	ColorRegister(ColorRegister &&o) noexcept : red(o.red), green(o.green), blue(o.blue) {}
	ColorRegister() : red(0), green(0), blue(0) {}
	ColorRegister(int r, int g, int b) : red(r), green(g), blue(b) {}

	ColorRegister &operator=(ColorRegister &&b) noexcept
	{
		red = b.red, green = b.green, blue = b.blue;
		return *this;
	}
	ColorRegister &operator=(const ColorRegister &b) noexcept
	{
		red = b.red, green = b.green, blue = b.blue;
		return *this;
	}
	bool operator==(const ColorRegister &b) const noexcept
	{
		return red == b.red && green == b.green && blue == b.blue;
	}
};

class Palette
{
public:
	Palette() {}
	Palette(std::vector<ColorRegister> &&colors) : Pal(std::move(colors)) { CalcBits(); }
	Palette(const std::vector<ColorRegister> &colors) : Pal(colors) { CalcBits(); }
	Palette(Palette &&o) noexcept : Pal(std::move(o.Pal)), NumBits(o.NumBits) {}
	Palette(const Palette &o) : Pal(o.Pal), NumBits(o.NumBits) {}
	Palette &operator=(Palette &&o) noexcept { Pal = std::move(o.Pal); NumBits = o.NumBits; return *this; }
	Palette &operator=(const Palette &o) { Pal = o.Pal; NumBits = o.NumBits; return *this; }
	ColorRegister &operator[](size_t i) { return Pal[i]; }
	const ColorRegister &operator[](size_t i) const { return Pal[i]; }
	bool operator!=(const Palette &o) const { return Pal != o.Pal; }
	bool operator==(const Palette &o) const { return Pal == o.Pal; }
	void resize(size_t newsize) { Pal.resize(newsize); CalcBits(); }
	size_t size() const { return Pal.size(); }
	size_t empty() const { return Pal.empty(); }
	int Bits() const { return NumBits; }

	// Return a palette extended to the nearest power of 2 length
	Palette Extend() const;

	// "Fix" the OCS palette by duplicating the high nibble into the low nibble.
	void FixOCS();

	// Create the half-bright colors in the palette.
	void MakeEHB();

	// Find the palette entry most similar to the requested color.
	int NearestColor(int r, int g, int b) const;

protected:
	Palette(std::vector<ColorRegister> &&colors, int numbits) : Pal(colors), NumBits(numbits) {}

private:
	std::vector<ColorRegister> Pal;
	int NumBits = 0;	// # of bits needed to represent the maximum value in this palette

	void CalcBits();
};

struct PlanarBitmap
{
	int Width = 0, Height = 0, Pitch = 0;
	int NumPlanes = 0;
	class Palette Palette;
	uint8_t *Planes[32]{nullptr};	// Points into PlaneData
	uint8_t *PlaneData = nullptr;
	int TransparentColor = -1;
	int Delay = 0;
	int Rate = 60;
	uint8_t Interleave = 0;
	int NumFrames = 0;				// A hint, not authoritative
	int ModeID = 0;

	PlanarBitmap(int w, int h, int nPlanes);
	PlanarBitmap(const PlanarBitmap &o);
	PlanarBitmap(PlanarBitmap &&o) noexcept;
	~PlanarBitmap();

	PlanarBitmap &operator=(PlanarBitmap &&o) noexcept;

	bool operator==(const PlanarBitmap &o) noexcept;
	bool operator!=(const PlanarBitmap &o) noexcept { return !(*this == o); }

	void FillBitplane(int plane, bool set);

	// destextrawidth is the number of pixels between the end of the row
	// in the source image and the end of the row in the dest image.
	void ToChunky(void *dest, int destextrawidth) const;
};

class ChunkyBitmap
{
public:
	int Width = 0, Height = 0, Pitch = 0, BytesPerPixel = 0;
	uint8_t *Pixels = nullptr;

	ChunkyBitmap() {}
	ChunkyBitmap(const PlanarBitmap &o, int scalex = 1, int scaley = 1);
	ChunkyBitmap(const ChunkyBitmap &o, int fillcolor);
	ChunkyBitmap(int w, int h, int bpp = 1);
	ChunkyBitmap(const ChunkyBitmap &o);
	ChunkyBitmap(ChunkyBitmap &&o) noexcept;
	ChunkyBitmap &operator=(ChunkyBitmap &&o) noexcept;
	~ChunkyBitmap();

	bool operator==(ChunkyBitmap& o) noexcept;
	bool IsEmpty() const noexcept { return Pixels == nullptr; }
	void Clear(bool release=true) noexcept;
	void SetSolidColor(int color) noexcept;

	// Expand an image in the upper left corner of the bitmap to fill the
	// entire bitmap.
	void Expand(int scalex, int scaley) noexcept;

	// Reduce higher bit depth image to 8-bits
	ChunkyBitmap RGBtoPalette(const Palette &pal, int dithermode) const;

	// Convert HAM to RGB
	ChunkyBitmap HAM6toRGB(const Palette &pal) const;
	ChunkyBitmap HAM8toRGB(const Palette &pal) const;

	// Describes an error diffusion kernel. An array of these, terminated with a
	// weight of 0, describes one kernel. Since a single weighting is often applied
	// to multiple pixels, this struct stores each weight once with a list of
	// pixels to add that weighted value to.
	struct Diffuser
	{
		uint16_t weight;		// .16 fixed point
		struct { int8_t x, y; } to[6];
	};

private:
	// Helper functions for Expand
	void Expand1(int scalex, int scaley, int srcwidth, int srcheight, const uint8_t *src, uint8_t *dest) noexcept;
	void Expand2(int scalex, int scaley, int srcwidth, int srcheight, const uint16_t *src, uint16_t *dest) noexcept;
	void Expand4(int scalex, int scaley, int srcwidth, int srcheight, const uint32_t *src, uint32_t *dest) noexcept;

	// Allocate the buffer
	void Alloc(int w, int h, int bpp);
};

class IFFChunk
{
public:
	IFFChunk(std::istream &file, uint32_t id, uint32_t len);
	~IFFChunk();

	uint32_t GetID() const noexcept { return ChunkID; }
	uint32_t GetLen() const noexcept { return ChunkLen; }
	const void *GetData() const noexcept { return ChunkData; }

private:
	uint32_t ChunkID;
	uint32_t ChunkLen;
	uint8_t *ChunkData;
};

class FORMReader
{
public:
	FORMReader(_TCHAR *filename, std::istream &file);
	FORMReader(_TCHAR *filename, std::istream &file, uint32_t len);
	~FORMReader();

	uint32_t GetID() { return FormID; }
	uint32_t GetLen() { return FormLen; }
	uint32_t GetPos() { return Pos; }
	bool NextChunk(IFFChunk **chunk, FORMReader **form);

private:
	std::istream &File;
	_TCHAR *Filename;
	uint32_t FormLen;
	uint32_t FormID;
	uint32_t Pos;
};


struct LogicalScreenDescriptor
{
	uint16_t Width;
	uint16_t Height;
	uint8_t Flags;
	uint8_t BkgColor;
	uint8_t AspectRatio;
};

struct GraphicControlExtension
{
	uint8_t ExtensionIntroducer;
	uint8_t GraphicControlLabel;
	uint8_t BlockSize;
	uint8_t Flags;
	uint16_t DelayTime;
	uint8_t TransparentColor;
	uint8_t BlockTerminator;
};

struct ImageDescriptor
{
	uint16_t Left;
	uint16_t Top;
	uint16_t Width;
	uint16_t Height;
	uint8_t Flags;
};

struct GIFFrame
{
	GIFFrame();
	GIFFrame(GIFFrame&& o) noexcept;
	GIFFrame &operator= (const GIFFrame &o);
	GIFFrame &operator= (GIFFrame &&o) noexcept;

	void SetDelay(int centisecs) { GCE.DelayTime = centisecs; }
	void SetDisposal(int method) { GCE.Flags = (GCE.Flags & 0x1C0) | (method << 2); }
	bool Write(FILE *file);

	GraphicControlExtension GCE;
	ImageDescriptor IMD;
	uint8_t LocalPalBits = 0;
	Palette LocalPalette;
	std::vector<uint8_t> LZW;
};

class Histogram
{
public:
	struct HistEntry
	{
		uint8_t Component[3];	// Red, Green, and Blue
		uint32_t Count;			// Number of pixels that have this color

		HistEntry(uint8_t c1, uint8_t c2, uint8_t c3)
		{
			Component[0] = c1;
			Component[1] = c2;
			Component[2] = c3;
			Count = 1;
		}
		HistEntry &operator=(const HistEntry &o)
		{
			Component[0] = o.Component[0];
			Component[1] = o.Component[1];
			Component[2] = o.Component[2];
			Count = o.Count;
			return *this;
		}
		explicit operator ColorRegister() const
		{
			return ColorRegister(Component[0], Component[1], Component[2]);
		}
	};
	HistEntry &operator[](size_t n) { return Histo[n]; }
	const HistEntry &operator[](size_t n) const { return Histo[n]; }
	size_t size() const { return Histo.size(); }
	bool empty() const { return Histo.empty(); }

	void AddPixels(const uint8_t *src, size_t numpixels, uint8_t mins[3], uint8_t maxs[3]);
	Palette ToPalette() const;

private:
	std::vector<HistEntry> Histo;	// Histogram entries
	std::unordered_map<uint32_t, size_t> ColorToHisto;	// Color to histogram index
};

class Quantizer
{
public:
	virtual ~Quantizer();
	virtual void AddPixels(const ChunkyBitmap &bitmap);
	virtual void AddPixels(const uint8_t *rgb, size_t count) = 0;
	virtual Palette GetPalette() = 0;
};

enum
{
	QUANTIZER_MedianCut,
	QUANTIZER_NeuQuant,

	NUM_QUANTIZERS
};

extern Quantizer *(*const QuantizerFactory[NUM_QUANTIZERS])(int);

// GIF frames are not written directly after processing, because ANIMs
// may or may not duplicate the initial frames at the end of the animation,
// depending on whether they are used as ANIM brushes or normal fullscreen
// ANIMs.
class GIFFrameQueue
{
public:
	GIFFrameQueue();
	~GIFFrameQueue();

	bool Enqueue(GIFFrame &&frame, const PlanarBitmap *source_bitmap);
	bool Flush();
	void SetDropFrames(int count) { FinalFramesToDrop = count; }
	int GetDropFrames() const { return FinalFramesToDrop; }
	GIFFrame *MostRecent() { return Queue.empty() ? nullptr : &Queue.back().first; }
	unsigned int Total() { return TotalQueued; }
	void SetFile(FILE *f) { File = f; }

private:
	// As long as this is at least as large as the maximum interleave, it
	// doesn't really matter what this is.
	enum { MAX_QUEUE_SIZE = 8 };

	bool Shift();

	FILE *File;
	size_t FinalFramesToDrop;		// ANIMs duplicate frames at the end to facilitate looping
	unsigned TotalQueued = 0;		// Total # of frames that have ever been queued (not just queued now)
	std::vector<std::pair<GIFFrame,PlanarBitmap>> Queue;		// oldest frames come first
	std::vector<PlanarBitmap> FirstFrames;		// For checking if we really want to drop frames
};

// Command Line options
struct Opts
{
	std::vector<std::pair<unsigned, unsigned>> Clips;
	tstring OutPathname;
	bool SoloMode = false;
	int ForcedRate = 0;
	int DiffusionMode = 1;
	int ScaleX = 1;
	int ScaleY = 1;
	bool AspectScale = true;

	bool ParseClip(_TCHAR *clipstr);
	void SortClips();
};

class GIFWriter
{
public:
	GIFWriter(const Opts &options);
	~GIFWriter();

	void AddFrame(const PlanarBitmap *bitmap, ChunkyBitmap &&chunky);

private:
	FILE *File = nullptr;
	tstring BaseFilename;
	ChunkyBitmap PrevFrame;
	GIFFrameQueue WriteQueue;
	uint32_t FrameCount = 0;
	uint32_t TotalTicks = 0;
	uint32_t GIFTime = 0;
	uint32_t FrameRate = 50;	// Default to PAL!
	LogicalScreenDescriptor LSD;
	uint8_t BkgColor = 0;
	uint16_t PageWidth = 0, PageHeight = 0;
	Palette GlobalPal;
	bool ForcedFrameRate;
	int DiffusionMode = 0;
	std::vector<std::pair<unsigned, unsigned>> Clips;

	bool SoloMode = false;
	int SFrameIndex = 0;	// In solo mode: Character index where frame number starts
	int SFrameLength = 0;	// In solo mode: Number of characters for frame number
	int SExtIndex = -1;		// In solo mode: Character index where extension starts
	tstring Filename;

	int FirstDelay = 0;		// Delay from the first frame

	void WriteHeader(bool loop);
	void MakeFrame(const PlanarBitmap *bitmap, ChunkyBitmap &&chunky, const Palette &pal, int mincodesize);
	void MinimumArea(const ChunkyBitmap &prev, const ChunkyBitmap &cur, ImageDescriptor &imd);
	void DetectBackgroundColor(const PlanarBitmap *bitmap, const ChunkyBitmap &chunky);
	uint8_t SelectDisposal(const PlanarBitmap *bitmap, const ImageDescriptor &imd, const ChunkyBitmap &chunky);
	int SelectTransparentColor(const ChunkyBitmap &prev, const ChunkyBitmap &now, const ImageDescriptor &imd);
	bool FinishFile();	// Finish writing the file. Returns true on success.
	void BadWrite();
	void CheckForIndexSpot();
	void GenFilename();
	void AddDelay(GIFFrame* frame, int delay);
};

#define ID_PP20 MAKE_ID('P','P','2','0')

void LoadFile(_TCHAR *filename, std::istream &file, GIFWriter &writer, const Opts &options);
std::unique_ptr<uint8_t[]> LoadPowerPackerFile(std::istream &file, size_t filesize, unsigned &unpackedsize);
void rotate8x8(unsigned char *src, int srcstep, unsigned char *dst, int dststep);
