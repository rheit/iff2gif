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
#include <filesystem>
#include <iostream>
#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#include "iff2gif.h"

static const uint16_t *Do8short(uint16_t *pixel, uint16_t *stop, const uint16_t *ops, uint16_t xormask, int pitch);

IFFChunk::IFFChunk(std::istream &file, uint32_t id, uint32_t len)
{
	ChunkID = id;
	ChunkLen = len;
	ChunkData = new uint8_t[len];

	if (!file.read(reinterpret_cast<char *>(ChunkData), len))
	{
		fprintf(stderr, "Only read %llu of %u bytes in chunk %4s\n", (unsigned long long)file.gcount(), len, (char *)&ChunkID);
		ChunkID = 0;
	}
	if (len & 1)
	{ // Skip padding byte after odd-sized chunks.
		file.seekg(1, std::ios_base::cur);
	}
}

IFFChunk::~IFFChunk()
{
	if (ChunkData != NULL)
	{
		delete[] ChunkData;
	}
}

FORMReader::FORMReader(_TCHAR *filename, std::istream &file)
	: File(file)
{
	Filename = filename;

	// If we rewind 4 bytes and read, we should get 'FORM'.
	file.read(reinterpret_cast<char*>(&FormLen), 4);
	file.read(reinterpret_cast<char*>(&FormID), 4);

	FormLen = BigLong(FormLen);
	Pos = 4;	// Length includes the FORM ID
}

FORMReader::FORMReader(_TCHAR *filename, std::istream &file, uint32_t len)
	: File(file)
{
	Filename = filename;
	FormLen = len;

	file.read(reinterpret_cast<char*>(&FormID), 4);
	Pos = 4;
}

FORMReader::~FORMReader()
{
	// Seek to end of FORM, so we're ready to read more data if it was
	// inside a container.
	uint32_t len = FormLen + (FormLen & 1);
	if (Pos != len)
	{
		File.seekg(len - Pos, std::ios_base::cur);
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
		uint32_t chunkhead[2];	// ID, Len
		if (File.read(reinterpret_cast<char *>(chunkhead), 4 * 2).good())
		{
			uint32_t id = chunkhead[0];
			uint32_t len = BigLong(chunkhead[1]);
			Pos += len + (len & 1) + 8;
			if (id == ID_FORM)
			{
				if (form == NULL)
				{
					File.seekg(len + (len & 1), std::ios_base::cur);
					return NextChunk(chunk, NULL);
				}
				*form = new FORMReader(Filename, File, len);
			}
			else
			{
				if (chunk == NULL)
				{
					File.seekg(len + (len & 1), std::ios_base::cur);
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

// Back in the early days of ILBM, when the Amiga only had 4 bits
// per color channel, it was common practice to write the colormap
// by just shifting each component left 4 bits. This is wrong on
// anything with a higher color depth, because everything ends up
// darker than intended.
static bool CheckOCSPalette(const Palette &pal)
{
	size_t i = 0;

	if (pal.size() <= 32)
	{
		for (i = 0; i < pal.size(); ++i)
		{
			if ((pal[i].red & 0x0F) != 0 ||
				(pal[i].green & 0x0F) != 0 ||
				(pal[i].blue & 0x0F) != 0)
			{
				break;
			}
		}
	}
	// If we make it all the way through the for loop, it's probably
	// an improperly written OCS palette.
	return i == pal.size();
}

void UnpackBody(PlanarBitmap *planes, BitmapHeader &header, uint32_t len, const void *data)
{
	const int8_t *in = (const int8_t *)data;
	//const int8_t *end = in + len;
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
void Delta5(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
{
	const uint32_t *planes = (const uint32_t *)delta;
	int numcols = (bitmap->Width + 7) / 8;
	int pitch = bitmap->Pitch;
	const uint8_t xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		uint32_t ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const uint8_t *ops = (const uint8_t *)delta + ptr;
		for (int x = 0; x < numcols; ++x)
		{
			uint8_t *pixel = bitmap->Planes[p] + x;
			uint8_t *stop = pixel + bitmap->Height * pitch;
			uint8_t opcount = *ops++;
			while (opcount-- > 0)
			{
				uint8_t op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					uint8_t cnt = op & 0x7F;
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
					uint8_t cnt = *ops++;
					uint8_t fill = *ops++;
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
void Delta7Short(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
{
	const uint32_t *lists = (const uint32_t *)delta;
	int numcols = (bitmap->Width + 15) / 16;
	int pitch = bitmap->Pitch / 2;
	const uint16_t xormask = (head->bits & ANIM_XOR) ? 0xFFFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		uint32_t opptr = BigLong(lists[p]);
		if (opptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const uint16_t *data = (const uint16_t *)((const uint8_t *)delta + BigLong(lists[p + 8]));
		const uint8_t *ops = (const uint8_t *)delta + opptr;
		for (int x = 0; x < numcols; ++x)
		{
			uint16_t *pixels = (uint16_t *)bitmap->Planes[p] + x;
			uint16_t *stop = pixels + bitmap->Height * pitch;
			uint8_t opcount = *ops++;
			while (opcount-- > 0)
			{
				uint8_t op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					uint8_t cnt = op & 0x7F;
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
					uint8_t cnt = *ops++;
					uint16_t fill = *data++;
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
void Delta7Long(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
{
	// ILBMs are only padded to 16 pixel widths, so what happens when the image
	// needs to be padded to 32 pixels for long data but isn't? The spec doesn't say.
	const uint32_t *lists = (const uint32_t *)delta;
	int numcols = (bitmap->Width + 15) / 32;
	int pitch = bitmap->Pitch;
	const uint32_t xormask = (head->bits & ANIM_XOR) ? 0xFFFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		uint32_t opptr = BigLong(lists[p]);
		if (opptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const uint32_t *data = (const uint32_t *)((const uint8_t *)delta + BigLong(lists[p + 8]));
		const uint8_t *ops = (const uint8_t *)delta + opptr;
		for (int x = 0; x < numcols; ++x)
		{
			uint32_t *pixels = (uint32_t *)bitmap->Planes[p] + x;
			uint32_t *stop = (uint32_t *)((uint8_t *)pixels + bitmap->Height * pitch);
			uint8_t opcount = *ops++;
			while (opcount-- > 0)
			{
				uint8_t op = *ops++;
				if (op & 0x80)
				{ // Uniq op: copy data literally
					uint8_t cnt = op & 0x7F;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ *data;
							pixels = (uint32_t *)((uint8_t *)pixels + pitch);
						}
						data++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					uint8_t cnt = *ops++;
					uint32_t fill = *data++;
					while (cnt-- > 0)
					{
						if (pixels < stop)
						{
							*pixels = (*pixels & xormask) ^ fill;
							pixels = (uint32_t *)((uint8_t *)pixels + pitch);
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixels = (uint32_t *)((uint8_t *)pixels + op * pitch);
				}
			}
		}
	}
}

// Short vertical delta using merged op and data lists, like op 5.
void Delta8Short(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
{
	const uint32_t *planes = (const uint32_t *)delta;
	int numcols = (bitmap->Width + 15) / 16;
	int pitch = bitmap->Pitch / 2;
	const uint16_t xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		uint32_t ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const uint16_t *ops = (const uint16_t *)((const uint8_t *)delta + ptr);
		for (int x = 0; x < numcols; ++x)
		{
			uint16_t *pixel = (uint16_t *)bitmap->Planes[p] + x;
			uint16_t *stop = pixel + bitmap->Height * pitch;
			ops = Do8short(pixel, stop, ops, xormask, pitch);
		}
	}
}

static const uint16_t *Do8short(uint16_t *pixel, uint16_t *stop, const uint16_t *ops, uint16_t xormask, int pitch)
{
	uint16_t opcount = BigShort(*ops++);
	while (opcount-- > 0)
	{
		uint16_t op = BigShort(*ops++);
		if (op & 0x8000)
		{ // Uniq op: copy data literally
			uint16_t cnt = op & 0x7FFF;
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
			uint16_t cnt = BigShort(*ops++);
			uint16_t fill = *ops++;
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
void Delta8Long(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
{
	const uint32_t *planes = (const uint32_t *)delta;
	int numcols = (bitmap->Width + 31) / 32;
	int pitch = bitmap->Pitch;
	bool lastisshort = (bitmap->Width & 16) != 0;
	const uint16_t xormask = (head->bits & ANIM_XOR) ? 0xFF : 0x00;
	for (int p = 0; p < bitmap->NumPlanes; ++p)
	{
		uint32_t ptr = BigLong(planes[p]);
		if (ptr == 0)
		{ // No ops for this plane.
			continue;
		}
		const uint32_t *ops = (const uint32_t *)((const uint8_t *)delta + ptr);
		for (int x = 0; x < numcols; ++x)
		{
			uint32_t *pixel = (uint32_t *)bitmap->Planes[p] + x;
			uint32_t *stop = (uint32_t *)((uint8_t *)pixel + bitmap->Height * pitch);
			if (x == numcols - 1 && lastisshort)
			{
				Do8short((uint16_t *)pixel, (uint16_t *)stop, (uint16_t *)ops, xormask, pitch / 2);
				continue;
			}
			uint32_t opcount = BigLong(*ops++);
			while (opcount-- > 0)
			{
				uint32_t op = BigLong(*ops++);
				if (op & 0x80000000)
				{ // Uniq op: copy data literally
					uint32_t cnt = op & 0x7FFFFFFF;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ *ops;
							pixel = (uint32_t *)((uint8_t *)pixel + pitch);
						}
						ops++;
					}
				}
				else if (op == 0)
				{ // Same op: copy one byte to several rows
					uint32_t cnt = BigLong(*ops++);
					uint32_t fill = *ops++;
					while (cnt-- > 0)
					{
						if (pixel < stop)
						{
							*pixel = (*pixel & xormask) ^ fill;
							pixel = (uint32_t *)((uint8_t *)pixel + pitch);
						}
					}
				}
				else
				{ // Skip op: Skip some rows
					pixel = (uint32_t *)((uint8_t *)pixel + op * pitch);
				}
			}
		}
	}
}

PlanarBitmap *ApplyDelta(PlanarBitmap *bitmap, AnimHeader *head, uint32_t len, const void *delta)
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
	PlanarBitmap *planes = nullptr;
	BitmapHeader header;
	AnimHeader anheader;
	bool anhdread = false;
	IFFChunk *chunk;
	int speed = -1;
	int numframes = 0;
	uint32_t modeid = 0;
	Palette palette;

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
			if (header.nPlanes == 0 ||
				(header.nPlanes > 8 && header.nPlanes != 24 && header.nPlanes != 32))
			{
				fprintf(stderr, "Invalid number of bitplanes (%u)\n", header.nPlanes);
				return NULL;
			}
			planes = new PlanarBitmap(header.w, header.h, header.nPlanes);
			if (header.masking == mskHasTransparentColor)
			{
				planes->TransparentColor = header.transparentColor;
			}
			planes->Rate = 60;
			break;
		}

		case ID_ANHD:
		{
			const uint8_t *ahdr = (const uint8_t *)chunk->GetData();
			anhdread = true;
			anheader.operation = ahdr[0];
			anheader.mask = ahdr[1];
			anheader.w = BigShort(*(uint16_t *)(ahdr + 2));
			anheader.h = BigShort(*(uint16_t *)(ahdr + 4));
			anheader.x = BigShort(*(int16_t *)(ahdr + 6));
			anheader.y = BigShort(*(int16_t *)(ahdr + 8));
			anheader.abstime = BigLong(*(uint32_t *)(ahdr + 10));
			anheader.reltime = BigLong(*(uint32_t *)(ahdr + 14));
			anheader.interleave = ahdr[18];
			anheader.bits = BigLong(*(uint32_t *)(ahdr + 20));
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
			palette.resize(palsize);
			memcpy(&palette[0], chunk->GetData(), chunk->GetLen());
			if (CheckOCSPalette(palette))
			{
				palette.FixOCS();
			}
			break;
		}

		case ID_CAMG:
			modeid = BigLong(*(const uint32_t *)chunk->GetData());
			break;

		case ID_DEST:
			// FIXME: DEST chunks should not be ignored.
			break;

		case ID_ANNO:
			printf("Annotation: %.*s\n", chunk->GetLen(), (char *)chunk->GetData());
			break;

		case ID_DPAN:
		{
			const DPAnimChunk *dpan = (const DPAnimChunk *)chunk->GetData();
			speed = dpan->speed;
			if (speed == 0)
			{ // probably an ANIM brush, so pretend it's 10 fps
				speed = 10;
			}
			// The DPAN chunk is optional, so its nframes field can ever
			// only be considered a hint. You should still read as many
			// frames as you can.
			numframes = BigShort(dpan->nframes);
			printf("%u frames @ %u fps\n", numframes, dpan->speed);
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
				delete planes;
				return NULL;
			}
			UnpackBody(planes, header, chunk->GetLen(), chunk->GetData());
			break;

		case ID_DLTA:
			if (!anhdread)
			{
				fprintf(stderr, "Delta chunk encountered before header\n");
				return NULL;
			}
			// If planes is not null, then there was a BMHD in this frame.
			// Use that if there is no history, otherwise prefer history.
			if (history != NULL && history[anheader.interleave & 1] != NULL)
			{
				if (planes != nullptr)
				{
					delete planes;
				}
				planes = history[anheader.interleave & 1];
			}
			if (planes == nullptr)
			{
				fprintf(stderr, "Delta chunk encountered without any history\n");
				return NULL;
			}
			planes = ApplyDelta(planes, &anheader, chunk->GetLen(), chunk->GetData());
			break;

		default:
			//printf("Ignoring chunk %c%c%c%c\n",
			//	chunk->GetID() & 255, (chunk->GetID() >> 8) & 255, (chunk->GetID() >> 16) & 255, chunk->GetID() >> 24);
			break;
		}
		delete chunk;
	}
	if (planes != NULL)
	{
		// Check for bogus CAMG like some brushes have, with junk in
		// upper word and extended bit NOT set in lower word.
		if ((modeid & 0xFFFF0000) && (!(modeid & EXTENDED_MODE)))
		{
			// Bad CAMG, so ignore CAMG and determine a mode based on page size.
			modeid = 0;
			if (header.pageWidth >= 640) modeid |= HIRES;
			if (header.pageHeight >= 400) modeid |= LACE;
		}
		if (modeid & EXTRA_HALFBRITE)
		{
			palette.MakeEHB();
		}
		// Only overwrite the palette if we loaded a new one.
		if (!palette.empty())
		{
			planes->Palette = palette;
		}
		if (modeid != 0)
		{
			planes->ModeID = modeid;
		}
		if (speed > 0)
		{
			planes->Rate = speed;
		}
		planes->NumFrames = numframes;
		return planes;
	}
	return NULL;
}

static void AddFrame(GIFWriter &writer, PlanarBitmap *bitmap, int scalex, int scaley, bool aspectscale)
{
	// Do aspect ratio correction for appropriate ModeIDs.
	if (aspectscale)
	{
		switch (bitmap->ModeID & (LACE | HIRES | SUPERHIRES))
		{
		case LACE:				scalex *= 2; break;
		case HIRES:				scaley *= 2; break;
		case SUPERHIRES:		scaley *= 4; break;
		case SUPERHIRES | LACE:	scaley *= 2; break;
		}
	}
	ChunkyBitmap chunky(*bitmap, scalex, scaley);
	if (bitmap->ModeID & HAM)
	{
		if (bitmap->NumPlanes <= 6)
		{
			if (bitmap->Palette.size() < 16)
				bitmap->Palette.resize(16);
			chunky = chunky.HAM6toRGB(bitmap->Palette);
		}
		else if (bitmap->NumPlanes <= 8)
		{
			if (bitmap->Palette.size() < 64)
				bitmap->Palette.resize(64);
			chunky = chunky.HAM8toRGB(bitmap->Palette);
		}
	}
	writer.AddFrame(bitmap, std::move(chunky));
}

static void LoadANIM(FORMReader &form, GIFWriter &writer, int scalex, int scaley, bool aspectscale)
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
				AddFrame(writer, planar, scalex, scaley, aspectscale);
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
					// Keep the palette in sync on both buffers
					if (planar == history[1])
					{
						history[0]->Palette = planar->Palette;
					}
					else
					{
						history[1]->Palette = planar->Palette;
					}
				}
			}
		}
		delete chunk;
	}
	if (history[0] != NULL) delete history[0];
	if (history[1] != NULL) delete history[1];
}

// This class from https://gist.github.com/mlfarrell/28ea0e7b10756042956b579781ac0dd8
struct membuf : std::streambuf
{
	membuf(char *begin, char *end) : begin(begin), end(end)
	{
		this->setg(begin, begin, end);
	}

	virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which = std::ios_base::in) override
	{
		if (dir == std::ios_base::cur)
			gbump((int)off);
		else if (dir == std::ios_base::end)
			setg(begin, end + off, end);
		else if (dir == std::ios_base::beg)
			setg(begin, begin + off, end);

		return gptr() - eback();
	}

	virtual pos_type seekpos(std::streampos pos, std::ios_base::openmode mode) override
	{
		return seekoff(pos - pos_type(off_type(0)), std::ios_base::beg, mode);
	}

	char *begin, *end;
};


void LoadFile(_TCHAR *filename, std::istream &file, GIFWriter &writer, int scalex, int scaley, bool aspectscale)
{
	uint32_t id = 0;

	if (file.read(reinterpret_cast<char *>(&id), 4).good())
	{
		if (id == ID_PP20)
		{
			std::filesystem::path p(filename);
			unsigned unpackedsize;
			std::unique_ptr<uint8_t[]> unpacked =
				LoadPowerPackerFile(file, (size_t)std::filesystem::file_size(p), unpackedsize);

			membuf sbuf((char *)unpacked.get(), (char *)unpacked.get() + unpackedsize);
			std::istream unppfile(&sbuf);
			LoadFile(filename, unppfile, writer, scalex, scaley, aspectscale);
			return;
		}
		if (id != ID_FORM)
		{
			_ftprintf(stderr, _T("%s is not an IFF FORM\n"), filename);
			return;
		}
		FORMReader iff(filename, file);
		uint32_t id = iff.GetID();
		if (id == ID_ILBM)
		{
			PlanarBitmap *planar = LoadILBM(iff, NULL);
			if (planar != NULL)
			{
				AddFrame(writer, planar, scalex, scaley, aspectscale);
				delete planar;
			}
		}
		else if (id == ID_ANIM)
		{
			LoadANIM(iff, writer, scalex, scaley, aspectscale);
		}
		else
		{
			fprintf(stderr, "Unsupported IFF type %4s", (char *)&id);
			return;
		}
	}
}
