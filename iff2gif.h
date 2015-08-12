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
	UBYTE *Planes[32];
	UBYTE *PlaneData;
	int TransparentColor;
	int Delay;
	int Rate;
	UBYTE Interleave;

	PlanarBitmap(int w, int h, int nPlanes);
	PlanarBitmap(const PlanarBitmap &o);
	~PlanarBitmap();

	void FillBitplane(int plane, bool set);
	void ToChunky(void *dest);
};

class IFFChunk
{
public:
	IFFChunk(FILE *file, ULONG id, ULONG len);
	~IFFChunk();

	ULONG GetID() { return ChunkID; }
	ULONG GetLen() { return ChunkLen; }
	const void *GetData() { return ChunkData; }

private:
	ULONG ChunkID;
	ULONG ChunkLen;
	UBYTE *ChunkData;
};

class FORMReader
{
public:
	FORMReader(_TCHAR *filename, FILE *file);
	FORMReader(_TCHAR *filename, FILE *file, ULONG len);
	~FORMReader();

	ULONG GetID() { return FormID; }
	ULONG GetLen() { return FormLen; }
	ULONG GetPos() { return Pos; }
	bool NextChunk(IFFChunk **chunk, FORMReader **form);

private:
	FILE *File;
	_TCHAR *Filename;
	ULONG FormLen;
	ULONG FormID;
	ULONG Pos;
};


struct LogicalScreenDescriptor
{
	UWORD Width;
	UWORD Height;
	UBYTE Flags;
	UBYTE BkgColor;
	UBYTE AspectRatio;
};

struct GraphicControlExtension
{
	UBYTE ExtensionIntroducer;
	UBYTE GraphicControlLabel;
	UBYTE BlockSize;
	UBYTE Flags;
	UWORD DelayTime;
	UBYTE TransparentColor;
	UBYTE BlockTerminator;
};

struct ImageDescriptor
{
	UWORD Left;
	UWORD Top;
	UWORD Width;
	UWORD Height;
	UBYTE Flags;
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
	std::vector<UBYTE> LZW;
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
	UBYTE *PrevFrame;
	GIFFrameQueue WriteQueue;
	ULONG FrameCount;
	ULONG TotalTicks;
	ULONG GIFTime;
	ULONG FrameRate;
	LogicalScreenDescriptor LSD;
	UBYTE BkgColor;
	UWORD PageWidth, PageHeight;
	ColorRegister GlobalPal[256];
	UBYTE GlobalPalBits;

	static int ExtendPalette(ColorRegister *dest, const ColorRegister *src, int numentries);
	void WriteHeader(bool loop);
	void MakeFrame(PlanarBitmap *bitmap, UBYTE *chunky);
	void MinimumArea(const UBYTE *prev, const UBYTE *cur, ImageDescriptor &imd);
	void DetectBackgroundColor(PlanarBitmap *bitmap, const UBYTE *chunky);
	UBYTE SelectDisposal(PlanarBitmap *bitmap, ImageDescriptor &imd, const UBYTE *chunky);
	int SelectTransparentColor(const UBYTE *prev, const UBYTE *now, const ImageDescriptor &imd, int pitch);
	void BadWrite();
};

void LoadFile(_TCHAR *filename, FILE *file, GIFWriter &writer);
void rotate8x8(unsigned char *src, int srcstep, unsigned char *dst, int dststep);
