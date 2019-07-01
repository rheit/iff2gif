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

static int usage(_TCHAR *progname)
{
	_ftprintf(stderr, _T(
"Usage: %s [-c <frames>] [-f] [-r <frame rate>] <source IFF> [dest GIF]\n"
"    -c  Clip out only the specified frames from the source. This is a comma-\n"
"        separated range of frames of the form \"start-end\" or single frames.\n"
"    -f  Save each frame to a separate file. If consecutive '0's are present\n"
"        at the end of [dest GIF], they will be replaced with the frame\n"
"        number. Otherwise, the frame number will be inserted before the\n"
"        .gif extension.\n"
"    -r  Specify a frame rate to use instead of the one in the ANIM.\n"),
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
	for (size_t i = 1; i < size(clips); ++i)
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
	std::vector<std::pair<unsigned, unsigned>> clips;

	while ((opt = getopt(argc, argv, "fr:c:")) != -1)
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
		default:
			return usage(argv[0]);
		}
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
		return 1;
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
	GIFWriter writer(outstring, solomode, forcedrate, clips);
	LoadFile(argv[1], infile, writer);
	return 0;
}
