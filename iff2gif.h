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
#include "types.h"
#include "iff.h"

struct PlanarBitmap
{
	int Width = 0, Height = 0, Pitch = 0;
	int NumPlanes = 0;
	std::vector<ColorRegister> Palette;
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
	~PlanarBitmap();

	void FillBitplane(int plane, bool set);

	// destextrawidth is the number of pixels between the end of the row
	// in the source image and the end of the row in the dest image.
	void ToChunky(void *dest, int destextrawidth) const;
};

struct ChunkyBitmap
{
	int Width = 0, Height = 0, Pitch = 0;
	uint8_t *Pixels = nullptr;

	ChunkyBitmap() {}
	ChunkyBitmap(const PlanarBitmap &o, int scalex = 1, int scaley = 1);
	ChunkyBitmap(const ChunkyBitmap &o, int fillcolor);
	ChunkyBitmap(ChunkyBitmap &&o) noexcept;
	ChunkyBitmap &ChunkyBitmap::operator=(ChunkyBitmap &&o) noexcept;
	~ChunkyBitmap();

	bool IsEmpty() noexcept { return Pixels == nullptr; }
	void Clear(bool release=true) noexcept;
	void SetSolidColor(int color) noexcept;

	// Expand an image in the upper left corner of the bitmap to fill the
	// entire bitmap.
	void Expand(int scalex, int scaley) noexcept;
};

class IFFChunk
{
public:
	IFFChunk(std::istream &file, uint32_t id, uint32_t len);
	~IFFChunk();

	uint32_t GetID() { return ChunkID; }
	uint32_t GetLen() { return ChunkLen; }
	const void *GetData() { return ChunkData; }

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
	std::vector<ColorRegister> LocalPalette;
	std::vector<uint8_t> LZW;
};

// GIF frames are not written directly after processing, because ANIMs
// may or may not duplicate the initial frames at the end of the animation,
// depending on whether they are used as ANIM brushes or normal fullscreen
// ANIMs.
class GIFFrameQueue
{
public:
	GIFFrameQueue();
	~GIFFrameQueue();

	bool Enqueue(GIFFrame &&frame);
	bool Flush();
	void SetDropFrames(int count) { FinalFramesToDrop = count; }
	GIFFrame* MostRecent() { return Queue.empty() ? nullptr : &Queue.back(); }
	unsigned int Total() { return TotalQueued; }
	void SetFile(FILE *f) { File = f; }

private:
	bool Shift();

	// As long as this is at least as large as the maximum interleave, it
	// doesn't really matter what this is.
	enum { MAX_QUEUE_SIZE = 8 };

	FILE *File;
	size_t FinalFramesToDrop;		// ANIMs duplicate frames at the end to facilitate looping
	std::queue<GIFFrame> Queue;		// oldest frames come first
	unsigned TotalQueued = 0;		// Total # of frames that have ever been queued (not just queued now)
};

class GIFWriter
{
public:
	GIFWriter(tstring filename, bool solo, int forcedrate, int scalex, int scaley,
		bool aspectscale, std::vector<std::pair<unsigned, unsigned>> &clips);
	~GIFWriter();

	void AddFrame(PlanarBitmap *bitmap);

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
	std::vector<ColorRegister> GlobalPal;
	uint8_t GlobalPalBits = 0;
	int ScaleX = 1, ScaleY = 1;
	bool AutoAspectScale;
	bool ForcedFrameRate;
	std::vector<std::pair<unsigned, unsigned>> Clips;

	bool SoloMode = false;
	int SFrameIndex = 0;	// In solo mode: Character index where frame number starts
	int SFrameLength = 0;	// In solo mode: Number of characters for frame number
	int SExtIndex = -1;		// In solo mode: Character index where extension starts
	tstring Filename;

	static int ExtendPalette(std::vector<ColorRegister> &dest, const std::vector<ColorRegister> &src);
	void WriteHeader(bool loop);
	void MakeFrame(PlanarBitmap *bitmap, ChunkyBitmap &&chunky);
	void MinimumArea(const ChunkyBitmap &prev, const ChunkyBitmap &cur, ImageDescriptor &imd);
	void DetectBackgroundColor(PlanarBitmap *bitmap, const ChunkyBitmap &chunky);
	uint8_t SelectDisposal(const PlanarBitmap *bitmap, const ImageDescriptor &imd, const ChunkyBitmap &chunky);
	int SelectTransparentColor(const ChunkyBitmap &prev, const ChunkyBitmap &now, const ImageDescriptor &imd);
	bool FinishFile();	// Finish writing the file. Returns true on success.
	void BadWrite();
	void CheckForIndexSpot();
	void GenFilename();
};

#define ID_PP20 MAKE_ID('P','P','2','0')

void LoadFile(_TCHAR *filename, std::istream &file, GIFWriter &writer);
std::unique_ptr<uint8_t[]> LoadPowerPackerFile(std::istream &file, size_t filesize, unsigned &unpackedsize);
void rotate8x8(unsigned char *src, int srcstep, unsigned char *dst, int dststep);
