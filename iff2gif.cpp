/* This file is part of iff2gif.
**
** Copyright 2015-2019 - Marisa Heit
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

#include <stdio.h>
#include <string>
#include <fstream>
#include "iff2gif.h"

int _tmain(int argc, _TCHAR* argv[])
{
	std::ifstream infile;
	tstring outstring;

	if (argc < 2 || argc > 3)
	{
		_ftprintf(stderr, _T("Usage: iff2gif <source IFF> [dest GIF]\n"));
		return 1;
	}

	infile.open(argv[1], std::ios_base::in | std::ios_base::binary);
	if (!infile.is_open())
	{
		_ftprintf(stderr, _T("Could not open %s: %s\n"), argv[1], _tcserror(errno));
		return 1;
	}
	if (argc == 3)
	{
		outstring = argv[2];
	}
	else
	{
		outstring = argv[1];

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
	GIFWriter writer(outstring);
	LoadFile(argv[1], infile, writer);
	return 0;
}
