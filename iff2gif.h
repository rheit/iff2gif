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

#include <vector>
#include "types.h"
#include "iff.h"

struct PlanarBitmap
{
	int Width, Height, Pitch;
	int NumPlanes;
	int PaletteSize;
	ColorRegister *Palette;
	uint8_t *Planes[32];
	uint8_t *PlaneData;
	int TransparentColor;
	int Delay;
	int Rate;
	uint8_t Interleave;

	PlanarBitmap(int w, int h, int nPlanes);
	PlanarBitmap(const PlanarBitmap &o);
	~PlanarBitmap();

	void FillBitplane(int plane, bool set);
	void ToChunky(void *dest);
};

class IFFChunk
{
public:
	IFFChunk(FILE *file, uint32_t id, uint32_t len);
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
	FORMReader(_TCHAR *filename, FILE *file);
	FORMReader(_TCHAR *filename, FILE *file, uint32_t len);
	~FORMReader();

	uint32_t GetID() { return FormID; }
	uint32_t GetLen() { return FormLen; }
	uint32_t GetPos() { return Pos; }
	bool NextChunk(IFFChunk **chunk, FORMReader **form);

private:
	FILE *File;
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
	GIFFrame &operator= (const GIFFrame &o);
	GIFFrame &operator= (GIFFrame &&o);

	void SetDelay(int centisecs) { GCE.DelayTime = centisecs; }
	void SetDisposal(int method) { GCE.Flags = (GCE.Flags & 0x1C0) | (method << 2); }
	bool Write(FILE *file);

	GraphicControlExtension GCE;
	ImageDescriptor IMD;
	std::vector<uint8_t> LZW;
};

class GIFFrameQueue
{
public:
	GIFFrameQueue();
	~GIFFrameQueue();

	bool Enqueue(GIFFrame &&frame);
	bool Flush();
	void SetDropFrames(int count) { FinalFramesToDrop = count; }
	GIFFrame *MostRecent() { return QueueCount > 0 ? &Queue[QueueCount - 1] : NULL; }
	void SetFile(FILE *f) { File = f; }

private:
	bool Shift();

	enum { QUEUE_SIZE = 2 };

	FILE *File;
	int QueueCount;
	int FinalFramesToDrop;			// ANIMs duplicate frames at the end to facilitate looping
	GIFFrame Queue[QUEUE_SIZE];		// oldest frames come first
};

class GIFWriter
{
public:
	GIFWriter(const _TCHAR *filename);
	~GIFWriter();

	void AddFrame(PlanarBitmap *bitmap);

private:
	FILE *File;
	const _TCHAR *Filename;
	uint8_t *PrevFrame;
	GIFFrameQueue WriteQueue;
	uint32_t FrameCount;
	uint32_t TotalTicks;
	uint32_t GIFTime;
	uint32_t FrameRate;
	LogicalScreenDescriptor LSD;
	uint8_t BkgColor;
	uint16_t PageWidth, PageHeight;
	ColorRegister GlobalPal[256];
	uint8_t GlobalPalBits;

	static int ExtendPalette(ColorRegister *dest, const ColorRegister *src, int numentries);
	void WriteHeader(bool loop);
	void MakeFrame(PlanarBitmap *bitmap, uint8_t *chunky);
	void MinimumArea(const uint8_t *prev, const uint8_t *cur, ImageDescriptor &imd);
	void DetectBackgroundColor(PlanarBitmap *bitmap, const uint8_t *chunky);
	uint8_t SelectDisposal(PlanarBitmap *bitmap, ImageDescriptor &imd, const uint8_t *chunky);
	int SelectTransparentColor(const uint8_t *prev, const uint8_t *now, const ImageDescriptor &imd, int pitch);
	void BadWrite();
};

void LoadFile(_TCHAR *filename, FILE *file, GIFWriter &writer);
void rotate8x8(unsigned char *src, int srcstep, unsigned char *dst, int dststep);
