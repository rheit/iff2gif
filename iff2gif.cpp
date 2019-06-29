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
"Usage: [-f] %s <source IFF> [dest GIF]\n"
"    -f  Save each frame to a separate file. If consecutive '0's are present\n"
"        at the end of [dest GIF], they will be replaced with the frame\n"
"        number. Otherwise, the frame number will be inserted before the\n"
"        .gif extension.\n"),
		progname);
	return 1;
}

int _tmain(int argc, _TCHAR* argv[])
{
	_TCHAR *inparm;
	std::ifstream infile;
	tstring outstring;
	int opt;
	bool solomode = false;

	while ((opt = getopt(argc, argv, "f")) != -1)
	{
		switch (opt)
		{
		case 'f':
			solomode = true;
			break;
		default:
			return usage(argv[0]);
		}
	}

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
	GIFWriter writer(outstring, solomode);
	LoadFile(argv[1], infile, writer);
	return 0;
}
