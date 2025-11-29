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
#include <algorithm>
#include "iff2gif.h"

#if defined(__linux__) || defined(__MACH__)
#include <cstring>
#include <climits>
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

bool Opts::ParseClip(_TCHAR *clipstr)
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
		Clips.push_back(std::make_pair(start, end));
	}
	return true;
}

void Opts::SortClips()
{
	// Sort by start frame.
	std::sort(begin(Clips), end(Clips));

	// Now check for overlapping or abutting ranges and combine them.
	for (size_t i = 1; i < Clips.size(); ++i)
	{
		if (Clips[i - 1].second >= Clips[i].first - 1)
		{
			Clips[i - 1].second = std::max(Clips[i - 1].second, Clips[i].second);
			Clips.erase(begin(Clips) + i);
			// Backup since we deleted an element and need to recheck entry i.
			--i;
		}
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	_TCHAR *inparm;
	std::ifstream infile;
	int opt;
	Opts options;

	while ((opt = getopt(argc, argv, "fr:c:x:y:s:nd:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			options.SoloMode = true;
			break;
		case 'r':
			options.ForcedRate = _ttoi(optarg);
			break;
		case 'c':
			if (!options.ParseClip(optarg))
				return 1;
			break;
		case 'x':
			options.ScaleX = _ttoi(optarg);
			break;
		case 'y':
			options.ScaleY = _ttoi(optarg);
			break;
		case 's':
			options.ScaleX = options.ScaleY = _ttoi(optarg);
			break;
		case 'n':
			options.AspectScale = false;
			break;
		case 'd':
			options.DiffusionMode = _ttoi(optarg);
			break;
		default:
			return usage(argv[0]);
		}
	}

	if (options.ScaleX < 1 || options.ScaleY < 1)
	{
		_ftprintf(stderr, _T("Scale must be at least 1\n"));
		return 1;
	}

	options.SortClips();

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
		options.OutPathname = argv[optind + 1];
	}
	else
	{
		options.OutPathname = inparm;

		// Strip off the existing extension if it's 4 or fewer characters.
		auto stop = options.OutPathname.find_last_of(_T('.'));
		if (stop != tstring::npos)
		{
			size_t extlen = options.OutPathname.size() - stop - 1;
			// "Real" extensions don't start with a space character
			if (extlen > 0 && extlen <= 4 && options.OutPathname[stop + 1] != _T(' '))
			{
				options.OutPathname.resize(stop);
			}
		}
		// Append the .gif extension to the input name.
		options.OutPathname += _T(".gif");
	}
	GIFWriter writer(options);
	LoadFile(argv[1], infile, writer, options);
	return 0;
}

#if defined(__linux__) || defined(__MACH__)
int main(int argc, char *argv[])
{
	return _tmain(argc, argv);
}
#endif
