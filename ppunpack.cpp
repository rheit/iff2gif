/* This file is partially based on code from pplib 1.2, placed in the
 * Public Domain on 25-Nov-2010 by Stuart Caie. It can be found on
 * Aminet at http://aminet.net/package/util/crypt/ppcrack.lha.
 *
 * PowerPacker file format
 * =======================
 * 		    4 bytes		Magic Identifier	PP20 or PX20
 * if PX20: 2 bytes		Checksum
 * 		    4 bytes		"Efficiency"
 * 		    [Crunched data, grouped into 4-byte chunks]
 * 		    3 bytes		Len of original data, big endian
 * 		    1 byte		"8 bits other info"
 *
 * PowerPacker encodes files backwards, from the end of the file to the
 * beginning. Not only do you write the output from the end to the beginning,
 * you also read the input data from the end to the beginning. Therefore, you
 * must read the entire file before you can decode any of it, making it
 * unsuitable for streaming.
 *
 * Crunched data is allegedly grouped into 4-byte chunks, but I doubt it
 * matters, since it's stored as a series of packed bits, so assuming
 * big-endian data, the first bit you extract will be in the final byte of
 * crunched data. So you can just treat it as a byte sequence. Bits are
 * shifted right out of the low bit of the input and left into the low bit of
 * whatever word you're reading. Note that this means that if you extract more
 * than one bit at a time, the number you get will be backwards compared to
 * what you need.
 *
 * The "8 bits other info" at the end of the file is a count of bits to read
 * before you get to the actual chunks of crunched data. I don't know what it
 * is, so skip it.
 *
 * The crunched data consists of a series of chunks, which you run through
 * until you've either decrunched the whole file, or you run out of input
 * (which would likely be a malformed file). Each chunk consists of optional
 * data to copy from the input. Every chunk copies data from an already
 * decoded part of the output to the current output position.
 *
 * If the first bit of a chunk is clear, you copy bytes from the input. The next
 * two (or more) bits determine how many bytes to copy. If 0, 1, or 2, add one
 * and copy that many bytes. If 3, add one, then read the next 2 bits and add
 * those to the number of bytes to read (do not add 1), repeating as long as you
 * keep reading a 3. The input data is read 8-bits at a time from the crunched
 * data bitstream and written downward in memory.
 *
 * The rest of the chunk describes a block of already decrunched data to copy to
 * the current output position. The next two bits determine both the size of the
 * data block to copy and the number of bits used to store the offset value.
 *
 * If 0, 1, or 2, add two to get the number of bytes to copy, and retrieve the
 * offset width from the corresponding entry of the Efficiency array. Then read
 * that many bits to get the offset value.
 *
 * For 3, add two to get an inital byte count, then check the next bit. If it is
 * clear, the offset width is 7 bits. Otherwise, the offset width from the fourth
 * entry of the Efficiency array is used. Read that many bits to get the offset
 * value. Then read the next three bits and add them to the byte count,
 * continuing as long as the value read was 7. This is similar to reading the
 * byte count when copying data from the source to the destination, except it's
 * done 3 bits at a time instead of 2.
 *
 * Once you have the byte count and the offset, copy bytes from the offset block
 * to the current output position. The offset is added to the position of the
 * most recently written byte and points to the last byte in the block to copy.
 */

#include <assert.h>
#ifdef _M_IX86
#include <intrin.h>
#endif
#include <fstream>

#include "iff2gif.h"

static const uint8_t ReversedBits[256] = {
	0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240,
	8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248,
	4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244,
	12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252,
	2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242,
	10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250,
	6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246,
	14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254,
	1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241,
	9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249,
	5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245,
	13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253,
	3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243,
	11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251,
	7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247,
	15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255
};

static uint32_t ReadReverse32(const uint8_t *p) noexcept
{
	return ReversedBits[p[0]] | (ReversedBits[p[1]] << 8) | (ReversedBits[p[2]] << 16) | (ReversedBits[p[3]] << 24);
}

class PPBitstream
{
public:
	PPBitstream(const uint8_t *lo, uintmax_t crunchedsize)
		: Bottom(lo), CurPos(lo + crunchedsize - 8)
	{
		// Copy the "Efficiency" values.
		for (int i = 0; i < 4; ++i)
		{
			StdOffsetWidths[i] = lo[4 + i];
		}

		// The final byte in the crunched file is the number of bits to skip
		// in the bitstream before we get to the compressed data.
		unsigned skipbits = lo[crunchedsize - 1];
		CurPos -= 4 * (skipbits / 32);
		skipbits &= 31;
		if (skipbits)
		{
			CheckBits(skipbits);
			ReadBits(skipbits);
		}
	}

	bool CheckBits(unsigned needed) noexcept
	{
		assert(needed <= 32);
		if (AvailBits < needed && CurPos >= Bottom)
		{
			// We reverse all 32 bits now so that we don't need to reverse them
			// a few at a time in ReadBits. PowerPacker is kind of weird in that
			// the whole bitstream is stored as a giant big endian number, and
			// you pull out bits from the least significant end (that is, the
			// actual end in memory), but multi-bit numbers are stored in
			// reverse order, so you can't just extract them out of memory with
			// a mask and shift without further processing, even on an Amiga.
			BitBuff.Put(ReadReverse32(CurPos), 32 - AvailBits);
			AvailBits += 32;
			CurPos -= 4;
		}
		return AvailBits >= needed;
	}

	uint32_t ReadBits(unsigned want) noexcept
	{
		assert(want > 0 && want < 32);
		assert(AvailBits >= want);
		AvailBits -= want;
		return BitBuff.Get(want);
	}

	unsigned GetOffsetWidth(unsigned idx)
	{
		assert(idx < 4);
		return StdOffsetWidths[idx];
	}

private:
	const uint8_t *Bottom;
	const uint8_t *CurPos;
	uint8_t StdOffsetWidths[4];
	unsigned AvailBits = 0;

	struct BitBuffFudge
	{
#if defined(_MSC_VER) && defined(_M_IX86)
		// Fudge about with the bitbuffer to avoid Visual C++ generating calls
		// to auxilliary functions on x86, since this is a pretty hot path.
		union
		{
			uint64_t buff64 = 0;
			uint32_t buff32[2];
		};

		void Put(uint32_t val, unsigned nbits) noexcept
		{
			if (nbits == 32)
				buff32[1] = val;
			else
				buff64 |= __ll_lshift(val, nbits);
		}
		uint32_t Get(unsigned nbits) noexcept
		{
			uint32_t out = buff32[1] >> (32 - nbits);
			buff64 = __ll_lshift(buff64, nbits);
			return out;
		}
#else
		uint64_t buff = 0;

		void Put(uint32_t val, unsigned nbits) noexcept
		{
			buff |= (uint64_t)val << nbits;
		}
		uint32_t Get(unsigned nbits) noexcept
		{
			uint32_t out = (uint32_t)(buff >> (64 - nbits));
			buff <<= nbits;
			return out;
		}
#endif
	};

	BitBuffFudge BitBuff;
};

bool PPUnpack(uint8_t *unpacked, unsigned unpackedsize, PPBitstream &bits)
{
	uint8_t *outp = unpacked + unpackedsize;
	unsigned todo, offsetwidth, offset, x;

	while (outp > unpacked && bits.CheckBits(3))
	{
		// Copy data directly from the bitstream to the destination?
		if (bits.ReadBits(1) == 0)
		{
			todo = 1;
			do {
				x = bits.ReadBits(2);
				todo += x;
			} while (x == 3 && bits.CheckBits(2));
			while (todo--)
			{
				if (outp <= unpacked) return false;
				bits.CheckBits(8);
				*--outp = bits.ReadBits(8);
			}
			if (outp == unpacked) return true;
			bits.CheckBits(2);
		}
		// Copy already-written data from elsewhere in the destination.
		x = bits.ReadBits(2);
		offsetwidth = bits.GetOffsetWidth(x);
		todo = x + 2;
		if (x == 3)
		{
			bits.CheckBits(1 + std::max(7u, offsetwidth) + 3);
			if (bits.ReadBits(1) == 0)
			{
				offsetwidth = 7u;
			}
			offset = bits.ReadBits(offsetwidth);
			do {
				x = bits.ReadBits(3);
				todo += x;
			} while (x == 7 && bits.CheckBits(3));
		}
		else
		{
			bits.CheckBits(offsetwidth);
			offset = bits.ReadBits(offsetwidth);
		}
		// The source byte range may overlap the destination byte range,
		// so we must not optimize with memcpy in that case.
		while (todo--)
		{
			if (outp <= unpacked) return false;
			*(outp - 1) = outp[offset];
			outp--;
		}
	}
	return outp == unpacked;
}

std::unique_ptr<uint8_t[]> LoadPowerPackerFile(std::istream &file, size_t filesize, unsigned &unpackedsize)
{
	std::unique_ptr<uint8_t[]> packed(new uint8_t[filesize]);
	unpackedsize = 0;
	file.seekg(0, file.beg);
	file.read((char *)packed.get(), filesize);
	if (file.gcount() != filesize)
	{
		return nullptr;
	}
	unpackedsize = (packed[filesize - 4] << 16) | (packed[filesize - 3] << 8) | packed[filesize - 2];
	std::unique_ptr<uint8_t[]> unpacked(new uint8_t[unpackedsize]);
	PPBitstream bits(packed.get(), filesize);
	if (PPUnpack(unpacked.get(), unpackedsize, bits))
	{
		return unpacked;
	}
	fprintf(stderr, "Failed to decompress PowerPacked data\n");
	return nullptr;
}