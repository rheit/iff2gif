/* This file is part of iff2gif.
**
** Copyright 2015 - Randy Heit
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
#include "iff2gif.h"

int _tmain(int argc, _TCHAR* argv[])
{
	FILE *infile;
	const _TCHAR *outname;
	std::basic_string<_TCHAR> outstring;

	if (argc < 2 || argc > 3)
	{
		_ftprintf(stderr, _T("Usage: iff2gif <source IFF> [dest GIF]\n"));
		return 1;
	}

	infile = _tfopen(argv[1], _T("rb"));
	if (infile == NULL)
	{
		_ftprintf(stderr, _T("Could not open %s: %s\n"), argv[1], _tcserror(errno));
		return 1;
	}
	if (argc == 3)
	{
		outname = argv[2];
	}
	else
	{
		// Replace the extension if it's 4 or fewer characters
		const _TCHAR *stop = _tcsrchr(argv[1], _T('.'));
		size_t extlen;
		if (stop != NULL)
		{
			extlen = argv[1] + _tcslen(argv[1]) - stop + 1;
			// "Real" extensions don't start with a space character
			if (extlen > 0 && stop[1] == _T(' '))
			{
				extlen = 0;
			}
		}
		if (extlen > 0 && extlen <= 4)
		{
			outstring = std::basic_string<_TCHAR>(argv[1], stop);
		}
		// Otherwise, just tack it on
		else
		{
			outstring = argv[1];
		}
		outstring += _T(".gif");
		outname = outstring.c_str();
	}
	GIFWriter writer(outname);
	LoadFile(argv[1], infile, writer);
	fclose(infile);
	return 0;
}
