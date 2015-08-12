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
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#include "iff2gif.h"

static const UWORD *Do8short(UWORD *pixel, UWORD *stop, const UWORD *ops, UWORD xormask, int pitch);

IFFChunk::IFFChunk(FILE *file, ULONG id, ULONG len)
{
	ChunkID = id;
	ChunkLen = len;
	ChunkData = new UBYTE[len];

	size_t bytesread = fread(ChunkData, 1, len, file);
	if (bytesread != len)
	{
		fprintf(stderr, "Only read %ud of %ud bytes in chunk %4s\n", bytesread, len, &ChunkID);
		ChunkID = 0;
	}
	if (len & 1)
	{ // Skip padding byte after odd-sized chunks.
		fseek(file, 1, SEEK_CUR);
	}
}

IFFChunk::~IFFChunk()
{
	if (ChunkData != NULL)
	{
		delete[] ChunkData;
	}
}

FORMReader::FORMReader(_TCHAR *filename, FILE *file)
{
	File = file;
	Filename = filename;

	// If we rewind 4 bytes and read, we should get 'FORM'.
	fread(&FormLen, 4, 1, file);
	fread(&FormID, 4, 1, file);

	FormLen = BigLong(FormLen);
	Pos = 4;	// Length includes the FORM ID
}

FORMReader::FORMReader(_TCHAR *filename, FILE *file, ULONG len)
{
	File = file;
	Filename = filename;
	FormLen = len;

	fread(&FormID, 4, 1, file);
	Pos = 4;
}

FORMReader::~FORMReader()
{
	// Seek to end of FORM, so we're ready to read more data if it was
	// inside a container.
	ULONG len = FormLen + (FormLen & 1);
	if (Pos != len)
	{
		fseek(File, len - Pos, SEEK_CUR);
	}
}

// Returns the next chunk in the FORM. This may be either a data chunk
// or another FORM. The appropriate pointer is filled accordingly, while
// the other is set to NULL. The returned object should be deleted before
// the calling NextChunk on this FORM again.
//
// Returns false when the end of the FORM has been reached.
//
// Either pointer can be NULL, in which case chunks of that type will be
// skipped.
bool FORMReader::NextChunk(IFFChunk **chunk, FORMReader **form)
{
	if (chunk != NULL)
	{
		*chunk = NULL;
	}
	if (form != NULL)
	{
		*form = NULL;
	}

	if (Pos < FormLen)
	{
		ULONG chunkhead[2];	// ID, Len
		if (fread(chunkhead, 4, 2, File) == 2)
		{
			ULONG id = chunkhead[0];
			ULONG len = BigLong(chunkhead[1]);

			Pos += len + (len & 1) + 8;
			if (id == ID_FORM)
			{
				if (form == NULL)
				{
					fseek(File, len + (len & 1), SEEK_CUR);
					return NextChunk(chunk, NULL);
				}
				*form = new FORMReader(Filename, File, len);
			}
			else
			{
				if (chunk == NULL)
				{
					fseek(File, len + (len & 1), SEEK_CUR);
					return NextChunk(NULL, form);
				}
				IFFChunk *chunker = new IFFChunk(File, id, len);
				if (chunker->GetID() == 0)
				{
					// Failed to read
					delete chunker;
					return false;
				}
				*chunk = chunker;
			}
			return true;
		}
	}
	return false;
}

// Some old OCS images have the bottom four bits zeroed for every entry.
// Fix them.
void FixOCSPalette(PlanarBitmap *planes)
{
	for (int i = 0; i < planes->PaletteSize; ++i)
	{
		planes->Palette[i].red |= planes->Palette[i].red >> 4;
		planes->Palette[i].green |= planes->Palette[i].green >> 4;
		planes->Palette[i].blue |= planes->Palette[i].blue >> 4;
	}
}

// In EHB mode, the palette has 64 entries, but the second 32 are implied
// as half intensity versions of the first 64.
void MakeEHBPalette(PlanarBitmap *planes)
{
	if (planes->Palette == NULL)
	{ // What palette?
		return;
	}
	if (planes->PaletteSize < 64)
	{
		ColorRegister *pal = new ColorRegister[64];
		memset(pal, 0, 64 * sizeof(ColorRegister));
		memcpy(pal, planes->Palette, planes->PaletteSize * sizeof(ColorRegister));
		delete[] planes->Palette;
		planes->Palette = pal;
		planes->PaletteSize = 64;
	}
	for (int i = 0; i < 32; ++i)
	{
		planes->Palette[32 + i].red = planes->Palette[i].red >> 1;
		planes->Palette[32 + i].green = planes->Palette[i].green >> 1;
		planes->Palette[32 + i].blue = planes->Palette[i].blue >> 1;
	}
}

void UnpackBody(PlanarBitmap *planes, BitmapHeader &header, ULONG len, const void *data)
{
	const BYTE *in = (const BYTE *)data;
	//const BYTE *end = in + len;
	// The mask plane is interleaved after the bitmap planes, so we need to count
	// it as another plane when reading.
	int nplanes = header.nPlanes + (header.masking == mskHasMask);
	int pitch = planes->Pitch;
	int out = 0;
	for (int y = 0; y < header.h; ++y)
	{
		for (int p = 0; p < nplanes; ++p)
		{
			if (p < header.nPlanes)
			{ // Read data into bitplane
				if (header.compression == cmpNone)
				{
					memcpy(&planes->Planes[p][out], in, pitch);
					in += pitch;
				}
				else for (int ofs = 0; ofs < pitch;)
				{
					if (*in >= 0)
					{
						memcpy(&planes->Planes[p][out + ofs], in + 1, *in + 1);
						ofs += *in + 1;
						in += *in + 2;
					}
					else
					{
						memset(&planes->Planes[p][out + ofs], in[1], -*in + 1);
						ofs += -*in + 1;
						in += 2;
					}
				}
			}
			else
			{ // This is the mask plane. Skip over it.
				if (header.compression == cmpNone)
				{
					in += pitch;
				}
				else for (int ofs = 0; ofs < pitch;)
				{
					if (*in >= 0)
					{
						ofs += *in + 1;
						in += *in + 2;
					}
					else
					{
						ofs += -*in + 1;
						in += 2;
					}
				}
			}
		}
		out += pitch;
	}
}

// Byte vertical delta: Probably the most common case by far
void Delta5(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	const ULONG *planes = (const ULONG *)delta;
	int numcols = (bitmap->Width + 7) / 8;
	int pitch = bitmap->Pitch;
	const UBYTE xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		ULONG ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const UBYTE *ops = (const UBYTE *)delta + ptr;
		for (int x = 0; x < numcols; ++x)
		{
			UBYTE *pixel = bitmap->Planes[p] + x;
			UBYTE *stop = pixel + bitmap->Height * pitch;
			UBYTE opcount = *ops++;
			while (opcount-- > 0)
			{
				UBYTE op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					UBYTE cnt = op & 0x7F;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ *ops;
							pixel += pitch;
						}
						ops++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					UBYTE cnt = *ops++;
					UBYTE fill = *ops++;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ fill;
							pixel += pitch;
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixel += op * pitch;
				}
			}
		}
	}
}

// Short vertical delta using separate op and data lists
void Delta7Short(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	const ULONG *lists = (const ULONG *)delta;
	int numcols = (bitmap->Width + 15) / 16;
	int pitch = bitmap->Pitch / 2;
	const UWORD xormask = (head->bits & ANIM_XOR) ? 0xFFFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		ULONG opptr = BigLong(lists[p]);
		if (opptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const UWORD *data = (const UWORD *)((const UBYTE *)delta + BigLong(lists[p + 8]));
		const UBYTE *ops = (const UBYTE *)delta + opptr;
		for (int x = 0; x < numcols; ++x)
		{
			UWORD *pixels = (UWORD *)bitmap->Planes[p] + x;
			UWORD *stop = pixels + bitmap->Height * pitch;
			UBYTE opcount = *ops++;
			while (opcount-- > 0)
			{
				UBYTE op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					UBYTE cnt = op & 0x7F;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ *data;
							pixels += pitch;
						}
						data++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					UBYTE cnt = *ops++;
					UWORD fill = *data++;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ fill;
							pixels += pitch;
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixels += op * pitch;
				}
			}
		}
	}
}

// Long vertical delta using separate op and data lists
void Delta7Long(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	// ILBMs are only padded to 16 pixel widths, so what happens when the image
	// needs to be padded to 32 pixels for long data but isn't? The spec doesn't say.
	const ULONG *lists = (const ULONG *)delta;
	int numcols = (bitmap->Width + 15) / 32;
	int pitch = bitmap->Pitch;
	const ULONG xormask = (head->bits & ANIM_XOR) ? 0xFFFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		ULONG opptr = BigLong(lists[p]);
		if (opptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const ULONG *data = (const ULONG *)((const UBYTE *)delta + BigLong(lists[p + 8]));
		const UBYTE *ops = (const UBYTE *)delta + opptr;
		for (int x = 0; x < numcols; ++x)
		{
			ULONG *pixels = (ULONG *)bitmap->Planes[p] + x;
			ULONG *stop = (ULONG *)((UBYTE *)pixels + bitmap->Height * pitch);
			UBYTE opcount = *ops++;
			while (opcount-- > 0)
			{
				UBYTE op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					UBYTE cnt = op & 0x7F;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ *data;
							pixels = (ULONG *)((UBYTE *)pixels + pitch);
						}
						data++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					UBYTE cnt = *ops++;
					ULONG fill = *data++;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ fill;
							pixels = (ULONG *)((UBYTE *)pixels + pitch);
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixels = (ULONG *)((UBYTE *)pixels + op * pitch);
				}
			}
		}
	}
}

// Short vertical delta using merged op and data lists, like op 5.
void Delta8Short(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	const ULONG *planes = (const ULONG *)delta;
	int numcols = (bitmap->Width + 15) / 16;
	int pitch = bitmap->Pitch / 2;
	const UWORD xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		ULONG ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const UWORD *ops = (const UWORD *)delta + ptr;
		for (int x = 0; x < numcols; ++x)
		{
			UWORD *pixel = (UWORD *)(bitmap->Planes[p] + x);
			UWORD *stop = pixel + bitmap->Height * pitch;
			ops = Do8short(pixel, stop, ops, xormask, pitch);
		}
	}
}

static const UWORD *Do8short(UWORD *pixel, UWORD *stop, const UWORD *ops, UWORD xormask, int pitch)
{
	UWORD opcount = BigShort(*ops++);
	while (opcount-- > 0)
	{
		UWORD op = BigShort(*ops++);
		if (op & 0x8000)
		{ // Uniq op: copy data literally
			UWORD cnt = op & 0x7FFF;
			while (cnt-- > 0)
			{
				if (pixel < stop)
				{
					*pixel = (*pixel & xormask) ^ *ops;
					pixel += pitch;
				}
				ops++;
			}
		}
		else if (op == 0)
		{ // Same op: copy one byte to several rows
			UWORD cnt = BigShort(*ops++);
			UWORD fill = *ops++;
			while (cnt-- > 0)
			{
				if (pixel < stop)
				{
					*pixel = (*pixel & xormask) ^ fill;
					pixel += pitch;
				}
			}
		}
		else
		{ // Skip op: Skip some rows
			pixel += op * pitch;
		}
	}
	return ops;
}

// Long vertical delta using merged op and data lists, like op 5.
// The final column uses shorts instead of longs if the bitmap is
// not an even number of 16-bit words wide.
void Delta8Long(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	const ULONG *planes = (const ULONG *)delta;
	int numcols = (bitmap->Width + 31) / 32;
	int pitch = bitmap->Pitch;
	bool lastisshort = (bitmap->Width & 16) != 0;
	const UWORD xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		ULONG ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const ULONG *ops = (const ULONG *)delta + ptr;
		for (int x = 0; x < numcols; ++x)
		{
			ULONG *pixel = (ULONG *)(bitmap->Planes[p] + x);
			ULONG *stop = (ULONG *)((UBYTE *)pixel + bitmap->Height * pitch);
			if (x == numcols - 1 && lastisshort)
			{
				Do8short((UWORD *)pixel, (UWORD *)stop, (UWORD *)ops, xormask, pitch / 2);
				continue;
			}
			ULONG opcount = BigLong(*ops++);
			while (opcount-- > 0)
			{
				ULONG op = BigLong(*ops++);
				if (op & 0x80000000)
				{ // Uniq op: copy data literally
					ULONG cnt = op & 0x7FFFFFFF;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ *ops;
							pixel = (ULONG *)((UBYTE *)pixel + pitch);
						}
						ops++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					ULONG cnt = BigLong(*ops++);
					ULONG fill = *ops++;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ fill;
							pixel = (ULONG *)((UBYTE *)pixel + pitch);
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixel = (ULONG *)((UBYTE *)pixel + op * pitch);
				}
			}
		}
	}
}

PlanarBitmap *ApplyDelta(PlanarBitmap *bitmap, AnimHeader *head, ULONG len, const void *delta)
{
	bitmap->Interleave = 2 - (head->interleave & 1);
	bitmap->Delay = head->reltime;
	switch (head->operation)
	{
	case 5:
		Delta5(bitmap, head, len, delta);
		break;

	case 7:
		if (head->bits & ANIM_LONG_DATA)
		{
			Delta7Long(bitmap, head, len, delta);
		}
		else
		{
			Delta7Short(bitmap, head, len, delta);
		}
		break;

	case 8:
		if (head->bits & ANIM_LONG_DATA)
		{
			Delta8Long(bitmap, head, len, delta);
		}
		else
		{
			Delta8Short(bitmap, head, len, delta);
		}
		break;

	default:
		fprintf(stderr, "Unhandled ANIM operation %d\n", head->operation);
		return NULL;
	}
	return bitmap;
}

PlanarBitmap *LoadILBM(FORMReader &form, PlanarBitmap *history[2])
{
	PlanarBitmap *planes = NULL;
	BitmapHeader header;
	AnimHeader anheader;
	IFFChunk *chunk;
	int speed = -1;
	ULONG modeid = 0;
	bool ocspal = false;

	while (form.NextChunk(&chunk, NULL))
	{
		switch (chunk->GetID())
		{
		case ID_BMHD:
		{
			const BitmapHeader *bhdr = (const BitmapHeader *)chunk->GetData();
			header.w = BigShort(bhdr->w);
			header.h = BigShort(bhdr->h);
			header.x = BigShort(bhdr->x);
			header.y = BigShort(bhdr->y);
			header.nPlanes = bhdr->nPlanes;
			header.masking = bhdr->masking;
			header.compression = bhdr->compression;
			header.pad1 = 0;
			header.transparentColor = BigShort(bhdr->transparentColor);
			header.xAspect = bhdr->xAspect;
			header.yAspect = bhdr->yAspect;
			header.pageWidth = BigShort(bhdr->pageWidth);
			header.pageHeight = BigShort(bhdr->pageHeight);
			planes = new PlanarBitmap(header.w, header.h, header.nPlanes);
			if (header.masking == mskHasTransparentColor)
			{
				planes->TransparentColor = header.transparentColor;
			}
			planes->Rate = 60;
			if (header.nPlanes > 8)
			{
				fprintf(stderr, "Too many bitplanes (%u)\n", header.nPlanes);
				return NULL;
			}
			break;
		}

		case ID_ANHD:
		{
			const UBYTE *ahdr = (const UBYTE *)chunk->GetData();
			anheader.operation = ahdr[0];
			anheader.mask = ahdr[1];
			anheader.w = BigShort(*(UWORD *)(ahdr + 2));
			anheader.h = BigShort(*(UWORD *)(ahdr + 4));
			anheader.x = BigShort(*(WORD *)(ahdr + 6));
			anheader.y = BigShort(*(WORD *)(ahdr + 8));
			anheader.abstime = BigLong(*(ULONG *)(ahdr + 10));
			anheader.reltime = BigLong(*(ULONG *)(ahdr + 14));
			anheader.interleave = ahdr[18];
			anheader.bits = BigLong(*(ULONG *)(ahdr + 20));
			if (anheader.interleave > 2)
			{
				fprintf(stderr, "Frame interleave of %u is more than 2\n", anheader.interleave);
				return NULL;
			}
			break;
		}

		case ID_CMAP:
		{
			int palsize = (chunk->GetLen() + 2) / 3;	// support truncated palettes
			planes->Palette = new ColorRegister[palsize];
			planes->PaletteSize = palsize;
			planes->Palette[palsize - 1].blue = planes->Palette[palsize - 1].green = 0;
			memcpy(planes->Palette, chunk->GetData(), chunk->GetLen());
			if (palsize <= 32)
			{
				int i;
				for (i = 0; i < palsize; ++i)
				{
					if ((planes->Palette[i].red & 0x0F) != 0 ||
						(planes->Palette[i].green & 0x0F) != 0 ||
						(planes->Palette[i].blue & 0x0F) != 0)
					{
						break;
					}
				}
				if (i == palsize)
				{ // Made it the whole way through. It's probably an
				  // improperly written OCS palette.
					ocspal = true;
				}
			}
			break;
		}

		case ID_CAMG:
			modeid = BigLong(*(const ULONG *)chunk->GetData());
			// Check for bogus CAMG like some brushes have, with junk in
			// upper word and extended bit NOT set not set in lower word.
			if ((modeid & 0xFFFF0000) && (!(modeid & EXTENDED_MODE)))
			{
				modeid = 0;
			}
			break;

		case ID_DEST:
			// FIXME: DEST chunks should not be ignored.
			break;

		case ID_ANNO:
			printf("Annotation: %.*s\n", chunk->GetLen(), chunk->GetData());
			break;

		case ID_DPAN:
		{
			const DPAnimChunk *dpan = (const DPAnimChunk *)chunk->GetData();
			speed = dpan->speed;
			if (speed == 0)
			{ // probably an ANIM brush, so pretend it's 10 fps
				speed = 10;
			}
			printf("%u frames @ %u fps\n", BigShort(dpan->nframes), dpan->speed);
			break;
		}

		case ID_BODY:
			if (planes == NULL)
			{
				fprintf(stderr, "BODY encountered before BMHD\n");
				return NULL;
			}
			if (header.compression > 1)
			{
				fprintf(stderr, "Unknown ILBM compression method #%d\n", header.compression);
				return NULL;
			}
			if (ocspal)
			{
				FixOCSPalette(planes);
			}
			if (modeid & EXTRA_HALFBRITE)
			{
				MakeEHBPalette(planes);
			}
			else if (modeid & HAM)
			{
				fprintf(stderr, "Note: HAM mode is not supported\n");
			}
			UnpackBody(planes, header, chunk->GetLen(), chunk->GetData());
			break;

		case ID_DLTA:
			if (history == NULL || history[anheader.interleave & 1] == NULL)
			{
				fprintf(stderr, "Delta chunk encountered without any history\n");
				return NULL;
			}
			planes = ApplyDelta(history[anheader.interleave & 1], &anheader, chunk->GetLen(), chunk->GetData());
			break;
		}
		delete chunk;
	}
	if (planes != NULL)
	{
		if (speed > 0)
		{
			planes->Rate = speed;
		}
		return planes;
	}
	return NULL;
}

static void LoadANIM(FORMReader &form, GIFWriter &writer)
{
	FORMReader *chunk;
	PlanarBitmap *history[2] = { NULL, NULL };

	while (form.NextChunk(NULL, &chunk))
	{
		if (chunk->GetID() == ID_ILBM)
		{
			PlanarBitmap *planar;
			while (NULL != (planar = LoadILBM(*chunk, history)))
			{
				writer.AddFrame(planar);
				if (history[0] == NULL)
				{ // This was the first frame. Duplicate it for double buffering.
					history[0] = planar;
					history[1] = new PlanarBitmap(*planar);
				}
				else
				{ // Swap buffers.
					if (planar->Interleave != 1)
					{
						std::swap(history[0], history[1]);
					}
				}
			}
		}
		delete chunk;
	}
	if (history[0] != NULL) delete history[0];
	if (history[1] != NULL) delete history[1];
}

void LoadFile(_TCHAR *filename, FILE *file, GIFWriter &writer)
{
	ULONG id = 0;

	if (fread(&id, 4, 1, file) == 1)
	{
		if (id != ID_FORM)
		{
			_ftprintf(stderr, _T("%s is not an IFF FORM\n"), filename);
			return;
		}
		FORMReader iff(filename, file);
		ULONG id = iff.GetID();
		if (id == ID_ILBM)
		{
			PlanarBitmap *planar = LoadILBM(iff, NULL);
			if (planar != NULL)
			{
				writer.AddFrame(planar);
				delete planar;
			}
		}
		else if (id == ID_ANIM)
		{
			LoadANIM(iff, writer);
		}
		else
		{
			fprintf(stderr, "Unsupported IFF type %4s", &id);
			return;
		}
	}
}
