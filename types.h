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

#include <stdlib.h>

#ifdef _WIN32
#include <tchar.h>

#ifdef _UNICODE
#define to_tstring std::to_wstring
#else
#define to_tstring std::to_string
#endif

#ifdef __cplusplus
extern "C" {
#endif
	extern int opterr, optind, optopt;
	extern _TCHAR *optarg;
	extern int getopt(int argc, _TCHAR **argv, const char *optstring);
#ifdef __cplusplus
}
#endif

#else
// These macros are a pain in the butt to use, but they seemed like the least
// amount of work to support Windows and other OSes with the same source.
#define _TCHAR char
#define _T(x) x
#define _ftprintf fprintf
#define _tfopen fopen
#define _tcslen strlen
#define _tcserror strerror
#define _tcsrchr strrchr
#define _tcschr strchr
#define _tcscmp strcmp
#define _ttoi atoi
#define _tcstok strtok
#define _tcspbrk strbrk
#define _tcstoul strtoul
#define to_tstring std::to_string
#endif

#ifdef __cplusplus
#include <string>
typedef std::basic_string<_TCHAR> tstring;

template <typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept
{
	return N;
}
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>

inline short LittleShort(short x)
{
	return (short)OSSwapLittleToHostInt16((uint16_t)x);
}

inline unsigned short LittleShort(unsigned short x)
{
	return OSSwapLittleToHostInt16(x);
}

inline short LittleShort(int x)
{
	return OSSwapLittleToHostInt16((uint16_t)x);
}

inline int LittleLong(int x)
{
	return OSSwapLittleToHostInt32((uint32_t)x);
}

inline unsigned int LittleLong(unsigned int x)
{
	return OSSwapLittleToHostInt32(x);
}

inline short BigShort(short x)
{
	return (short)OSSwapBigToHostInt16((uint16_t)x);
}

inline unsigned short BigShort(unsigned short x)
{
	return OSSwapBigToHostInt16(x);
}

inline int BigLong(int x)
{
	return OSSwapBigToHostInt32((uint32_t)x);
}

inline unsigned int BigLong(unsigned int x)
{
	return OSSwapBigToHostInt32(x);
}

#else
#ifdef __BIG_ENDIAN__

// Swap 16bit, that is, MSB and LSB byte.
// No masking with 0xFF should be necessary. 
inline short LittleShort(short x)
{
	return (short)((((unsigned short)x) >> 8) | (((unsigned short)x) << 8));
}

inline unsigned short LittleShort(unsigned short x)
{
	return (unsigned short)((x >> 8) | (x << 8));
}

// Swapping 32bit.
inline unsigned int LittleLong(unsigned int x)
{
	return (unsigned int)(
		(x >> 24)
		| ((x >> 8) & 0xff00)
		| ((x << 8) & 0xff0000)
		| (x << 24));
}

inline int LittleLong(int x)
{
	return (int)(
		(((unsigned int)x) >> 24)
		| ((((unsigned int)x) >> 8) & 0xff00)
		| ((((unsigned int)x) << 8) & 0xff0000)
		| (((unsigned int)x) << 24));
}

#define BigShort(x)		(x)
#define BigLong(x)		(x)

#else

#define LittleShort(x)		(x)
#define LittleLong(x) 		(x)

#if defined(_MSC_VER)

inline short BigShort(short x)
{
	return (short)_byteswap_ushort((unsigned short)x);
}

inline unsigned short BigShort(unsigned short x)
{
	return _byteswap_ushort(x);
}

inline int BigLong(int x)
{
	return (int)_byteswap_ulong((unsigned long)x);
}

inline unsigned int BigLong(unsigned int x)
{
	return (unsigned int)_byteswap_ulong((unsigned long)x);
}
#pragma warning (default: 4035)

#else

inline short BigShort(short x)
{
	return (short)((((unsigned short)x) >> 8) | (((unsigned short)x) << 8));
}

inline unsigned short BigShort(unsigned short x)
{
	return (unsigned short)((x >> 8) | (x << 8));
}

inline unsigned int BigLong(unsigned int x)
{
	return (unsigned int)(
		(x >> 24)
		| ((x >> 8) & 0xff00)
		| ((x << 8) & 0xff0000)
		| (x << 24));
}

inline int BigLong(int x)
{
	return (int)(
		(((unsigned int)x) >> 24)
		| ((((unsigned int)x) >> 8) & 0xff00)
		| ((((unsigned int)x) << 8) & 0xff0000)
		| (((unsigned int)x) << 24));
}
#endif

#endif // __BIG_ENDIAN__
#endif // __APPLE__
#endif // __cplusplus

#ifndef __BIG_ENDIAN__
#define MAKE_ID(a,b,c,d)	((a)|((b)<<8)|((c)<<16)|((d)<<24))
#else
#define MAKE_ID(a,b,c,d)	((d)|((c)<<8)|((b)<<16)|((a)<<24))
#endif

