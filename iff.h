typedef uint8_t Masking;		/* Choice of masking technique. */

#define mskNone					0
#define mskHasMask				1
#define mskHasTransparentColor	2
#define mskLasso				3

typedef uint8_t Compression;	/* Choice of compression algorithm
	applied to the rows of all source and mask planes. "cmpByteRun1"
	is the byte run encoding described in Appendix C. Do not compress
	across rows! */
#define cmpNone			0
#define cmpByteRun1		1

/* defines used for Modes in Amiga viewports (and therefore CAMG chunks) */

#define GENLOCK_VIDEO	0x0002
#define LACE			0x0004
#define SUPERHIRES		0x0020
#define PFBA			0x0040
#define EXTRA_HALFBRITE	0x0080
#define GENLOCK_AUDIO	0x0100
#define DUALPF			0x0400
#define HAM				0x0800
#define EXTENDED_MODE	0x1000
#define VP_HIDE			0x2000
#define SPRITES			0x4000
#define HIRES			0x8000

typedef struct {
	uint16_t	w, h;			/* raster width & height in pixels			*/
	int16_t		x, y;			/* pixel position for this image			*/
	uint8_t		nPlanes;		/* # source bitplanes						*/
	Masking		masking;
	Compression	compression;
	uint8_t		pad1;			/* unused; ignore on read, write as 0		*/
	uint16_t	transparentColor; /* transparent "color number" (sort of)	*/
	uint8_t		xAspect, yAspect; /* pixel aspect, a ratio width : height	*/
	int16_t		pageWidth, pageHeight; /* source "page" size in pixels		*/
} BitmapHeader;

typedef struct {
	uint8_t red, green, blue;		/* color intensities 0..255 */
} ColorRegister;					/* size = 3 bytes			*/

static bool operator== (const ColorRegister &a, const ColorRegister &b) noexcept
{
	return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

typedef struct {
	uint8_t depth;		/* # bitplanes in the original source				*/
	uint8_t pad1;		/* unused; for consistency put 0 here				*/
	uint16_t planePick;	/* how to scatter source bitplanes into destination	*/
	uint16_t planeOnOff;/* default bitplane data for planePick				*/
	uint16_t planeMask;	/* selects which bitplanes to store into			*/
} Destmerge;

typedef uint16_t SpritePrecedence;	/* relative precedence, 0 is the highest*/

#define ID_FORM		MAKE_ID('F','O','R','M')
#define ID_ILBM		MAKE_ID('I','L','B','M')
#define ID_BMHD		MAKE_ID('B','M','H','D')
#define ID_CMAP		MAKE_ID('C','M','A','P')
#define ID_GRAB		MAKE_ID('G','R','A','B')
#define ID_DEST		MAKE_ID('D','E','S','T')
#define	ID_SPRT		MAKE_ID('S','P','R','T')
#define ID_CAMG		MAKE_ID('C','A','M','G')
#define ID_BODY		MAKE_ID('B','O','D','Y')
#define ID_ANNO		MAKE_ID('A','N','N','O')

/* values for AnimHeader bits (mostly just for mode 4) */
#define ANIM_LONG_DATA	1	/* else short */
#define ANIM_XOR		2	/* else set */
#define ANIM_1INFOLIST	4	/* else separate info */
#define ANIM_RLC		8	/* else not RLC */
#define ANIM_VERT		16	/* else horizontal */
#define ANIM_LONGOFFS	32	/* else short offsets */

typedef struct {
	uint8_t	 operation;	/* the compression method							*/	
	uint8_t	 mask;		/* mode 1 only: plane mask where data is			*/
	uint16_t w, h;		/* mode 1 only: size of the changed area			*/
	int16_t	 x, y;		/* mode 1 only: position of the changed area		*/
	uint32_t abstime;	/* unused											*/
	uint32_t reltime;	/* jiffies (1/60 sec) to wait before flipping		*/
	uint8_t	 interleave;/* how many frames back this data is to modify		*/
	uint8_t	 pad0;
	uint32_t bits;		/* option bits										*/
} AnimHeader;

typedef struct {
	uint16_t version;	/* current version=4 */
	uint16_t nframes;	/* number of frames in the animation.*/
	uint8_t speed;		/* speed in fps */
	uint8_t pad[3];		/* Not used */
} DPAnimChunk;

#define ID_ANIM		MAKE_ID('A','N','I','M')
#define ID_ANHD		MAKE_ID('A','N','H','D')
#define ID_DPAN		MAKE_ID('D','P','A','N')
#define ID_DLTA		MAKE_ID('D','L','T','A')
