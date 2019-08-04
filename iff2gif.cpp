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

#include <stdio.h>
#include <string>
#include <fstream>
#include "iff2gif.h"

#ifdef __linux__
#include <cstring>
#include <climits>
#include <algorithm>
#include <getopt.h>

#undef _tcspbrk
#define _tcspbrk strpbrk
#endif

static int usage(_TCHAR *progname)
{
	_ftprintf(stderr, _T(
"Usage: %s [options] <source IFF> [dest GIF]\n"
"  Options:\n"
"    -c <frames>      Clip out only the specified frames from the source.\n"
"                     This is a comma-separated range of frames of the\n"
"                     form \"start-end\" or a single frame number.\n"
"    -f               Save each frame to a separate file. If consecutive\n"
"                     '0's are present at the end of [dest GIF], they will\n"
"                     be replaced with the frame number. Otherwise, the\n"
"                     frame number will be inserted before the .gif\n"
"                     extension.\n"
"    -n               No aspect ratio correction for (super)hires/interlace.\n"
"    -r <frame rate>  Override the frame rate from the ANIM.\n"
"    -x <x scale>     Scale image horizontally. Must be at least 1.\n"
"    -y <y scale>     Scale image vertically. Must be at least 1.\n"
"    -s <scale>       Set both horizontal and vertical scale.\n"
),
		progname);
	return 1;
}

static bool parseclip(std::vector<std::pair<unsigned, unsigned>> &clips, _TCHAR *clipstr)
{
	// Split into comma-seperated values
	for (_TCHAR *tok = _tcstok(clipstr, _T(",")); tok != nullptr; tok = _tcstok(nullptr, _T(",")))
	{
		_TCHAR *brk = _tcspbrk(tok, _T(":-"));
		unsigned start, end;
		if (brk == nullptr)
		{
			// Only one value, no range: Extract a single frame.
			start = end = _tcstoul(tok, nullptr, 10);
		}
		else
		{
			_TCHAR *endptr;
			start = (unsigned)_tcstoul(tok, &endptr, 10);
			if (endptr > brk) start = 1u;	// check for when the initial frame is omitted
			end = (unsigned)_tcstoul(brk + 1, nullptr, 10);
			if (end == 0) end = UINT_MAX;
		}
		if (end < start)
		{
			_ftprintf(stderr, _T("Start of range must come before the end\n"));
			return false;
		}
		clips.push_back(std::make_pair(start, end));
	}
	return true;
}

void sortclips(std::vector<std::pair<unsigned, unsigned>> &clips)
{
	// Sort by start frame.
	std::sort(begin(clips), end(clips));

	// Now check for overlapping or abutting ranges and combine them.
	for (size_t i = 1; i < clips.size(); ++i)
	{
		if (clips[i - 1].second >= clips[i].first - 1)
		{
			clips[i - 1].second = std::max(clips[i - 1].second, clips[i].second);
			clips.erase(begin(clips) + i);
			// Backup since we deleted an element and need to recheck entry i.
			--i;
		}
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	_TCHAR *inparm;
	std::ifstream infile;
	tstring outstring;
	int opt;
	bool solomode = false;
	int forcedrate = 0;
	int diffusionmode = 1;
	int scalex = 1, scaley = 1;
	bool aspectscale = true;
	std::vector<std::pair<unsigned, unsigned>> clips;

	while ((opt = getopt(argc, argv, "fr:c:x:y:s:nd:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			solomode = true;
			break;
		case 'r':
			forcedrate = _ttoi(optarg);
			break;
		case 'c':
			if (!parseclip(clips, optarg))
				return 1;
			break;
		case 'x':
			scalex = _ttoi(optarg);
			break;
		case 'y':
			scaley = _ttoi(optarg);
			break;
		case 's':
			scalex = scaley = _ttoi(optarg);
			break;
		case 'n':
			aspectscale = false;
			break;
		case 'd':
			diffusionmode = _ttoi(optarg);
			break;
		default:
			return usage(argv[0]);
		}
	}

	if (scalex < 1 || scaley < 1)
	{
		_ftprintf(stderr, _T("Scale must be at least 1\n"));
		return 1;
	}

	sortclips(clips);

	if (optind >= argc)
	{
		return usage(argv[0]);
	}
	inparm = argv[optind];
	infile.open(inparm, std::ios_base::in | std::ios_base::binary);
	if (!infile.is_open())
	{
		_ftprintf(stderr, _T("Could not open %s: %s\n"), inparm, _tcserror(errno));
		return 2;
	}
	if (optind + 1 < argc)
	{
		outstring = argv[optind + 1];
	}
	else
	{
		outstring = inparm;

		// Strip off the existing extension if it's 4 or fewer characters.
		auto stop = outstring.find_last_of(_T('.'));
		if (stop != tstring::npos)
		{
			size_t extlen = outstring.size() - stop - 1;
			// "Real" extensions don't start with a space character
			if (extlen > 0 && extlen <= 4 && outstring[stop + 1] != _T(' '))
			{
				outstring.resize(stop);
			}
		}
		// Append the .gif extension to the input name.
		outstring += _T(".gif");
	}
	GIFWriter writer(outstring, solomode, forcedrate, clips, diffusionmode);
	LoadFile(argv[1], infile, writer, scalex, scaley, aspectscale);
	return 0;
}

#ifdef __linux__
int main(int argc, char *argv[])
{
	return _tmain(argc, argv);
}
#endif
