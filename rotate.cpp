/*
=====================================================================
rotate.c
EW  21 Oct 93

A fast 90-degree bit rotation routine.

INPUTS
src       starting address of 8 x 8 bit source tile
srcstep   byte offset between adjacent rows in source
dst       starting address of 8 x 8 bit destination tile
dststep   byte offset between adjacent rows in destination

RESULTS
Bits from the source are rotated 90 degrees clockwise and written
to the destination.

From Sue-Ken Yap, "A Fast 90-Degree Bitmap Rotator," in GRAPHICS GEMS
II, James Arvo ed., Academic Press, 1991, pp. 84-85 and 514-515.

Note that by setting dststep = 1, eight 1-bit rows (1 scanline of an
8-plane bitmap) can be converted to one 8-bit (byte-per-pixel) row,
8 pixels at a time.
===================================================================== */

typedef unsigned int bit32;

#define table( name, n ) \
   static bit32 name[ 16 ] = { \
      static_cast<bit32>(0x00000000<<n),static_cast<bit32>(0x00000001<<n),static_cast<bit32>(0x00000100<<n),static_cast<bit32>(0x00000101<<n), \
      static_cast<bit32>(0x00010000<<n),static_cast<bit32>(0x00010001<<n),static_cast<bit32>(0x00010100<<n),static_cast<bit32>(0x00010101<<n), \
      static_cast<bit32>(0x01000000<<n),static_cast<bit32>(0x01000001<<n),static_cast<bit32>(0x01000100<<n),static_cast<bit32>(0x01000101<<n), \
      static_cast<bit32>(0x01010000<<n),static_cast<bit32>(0x01010001<<n),static_cast<bit32>(0x01010100<<n),static_cast<bit32>(0x01010101<<n) };

table(ltab0, 0)
table(ltab1, 1)
table(ltab2, 2)
table(ltab3, 3)
table(ltab4, 4)
table(ltab5, 5)
table(ltab6, 6)
table(ltab7, 7)


void rotate8x8(unsigned char *src, int srcstep, unsigned char *dst, int dststep)
{
	unsigned char *p;
	int    pstep, lonyb, hinyb;
	bit32  lo, hi;

	lo = hi = 0;

#define extract( d, t ) \
   lonyb = *d & 0xF; hinyb = *d >> 4; \
   lo |= t[ lonyb ]; hi |= t[ hinyb ]; d += pstep;

	p = src; pstep = srcstep;
	extract(p, ltab0)
		extract(p, ltab1)
		extract(p, ltab2)
		extract(p, ltab3)
		extract(p, ltab4)
		extract(p, ltab5)
		extract(p, ltab6)
		extract(p, ltab7)

#define unpack( d, w ) \
   *d = ( w >> 24 ) & 0xFF; d += pstep; \
   *d = ( w >> 16 ) & 0xFF; d += pstep; \
   *d = ( w >>  8 ) & 0xFF; d += pstep; \
   *d = w & 0xFF;

		p = dst; pstep = dststep;
	unpack(p, hi)
		p += pstep;
	unpack(p, lo)
}
