/* The public domain AT&T getopt, modified to work with Windows' widechars. */
/*
Relay - Version: version B 2.10 5 / 3 / 83; site utzoo.UUCP
Posting - Version: version B 2.10.2 9 / 18 / 84; site ut - sally.UUCP
Path : utzoo!decvax!decwrl!ucbvax!ucdavis!lll - crg!seismo!ut - sally!std - unix
	From : std - u...@ut-sally.UUCP(Moderator, John Quarterman)
	Newsgroups: mod.std.unix
	Subject : public domain AT &T getopt source
	Message - ID : <3352@ut-sally.UUCP>
	Date : Sun, 3 - Nov - 85 14 : 34 : 15 EST
	Article - I.D. : ut - sally.3352
	Posted : Sun Nov  3 14 : 34 : 15 1985
	Date - Received : Mon, 4 - Nov - 85 21 : 36 : 01 EST
	Organization : IEEE / P1003 Portable Operating System Environment Committee
	Lines : 91
	Approved : j...@ut-sally.UUCP

	Here's something you've all been waiting for:  the AT & T public domain
	source for getopt(3).It is the code which was given out at the 1985
	UNIFORUM conference in Dallas.I obtained it by electronic mail
	directly from AT & T.The people there assure me that it is indeed
	in the public domain.

	There is no manual page.That is because the one they gave out at
	UNIFORUM was slightly different from the current System V Release 2
	manual page.The difference apparently involved a note about the
	famous rules 5 and 6, recommending using white space between an option
	and its first argument, andnot grouping options that have arguments.
	Getopt itself is currently lenient about both of these things White
	space is allowed, but not mandatory, andthe last option in a group can
	have an argument.That particular version of the man page evidently
	has no official existence, andmy source at AT & T did not send a copy.
	The current SVR2 man page reflects the actual behavor of this getopt.
	However, I am not about to post a copy of anything licensed by AT & T.

	I will submit this source to Berkeley as a bug fix.

	I, personally, make no claims or guarantees of any kind about the
	following source.I did compile it to get some confidence that
	it arrived whole, but beyond that you're on your own.
*/

/* I assume if you're not compiling on Windows, you'd rather use the
 * system-provided getopt.
 */
#ifdef _WIN32
#include <stdio.h>
#include "types.h"

#define ERR(s, c)	if(opterr) { _ftprintf(stderr, _T("%s%s%c\n"), argv[0], s, c); }

int	opterr = 1;
int	optind = 1;
int	optopt;
_TCHAR *optarg;

int getopt(int argc, _TCHAR **argv, const char *opts)
{
	static int sp = 1;
	int c;
	char *cp;

	if (sp == 1)
		if (optind >= argc ||
			argv[optind][0] != '-' || argv[optind][1] == '\0')
			return -1;
		else if (_tcscmp(argv[optind], _T("--")) == 0) {
			optind++;
			return -1;
		}
	optopt = c = argv[optind][sp];
	if (c == ':' || (cp = strchr(opts, c)) == NULL) {
		ERR(_T(": illegal option -- "), c);
		if (argv[optind][++sp] == '\0') {
			optind++;
			sp = 1;
		}
		return '?';
	}
	if (*++cp == ':') {
		if (argv[optind][sp + 1] != '\0')
			optarg = &argv[optind++][sp + 1];
		else if (++optind >= argc) {
			ERR(_T(": option requires an argument -- "), c);
			sp = 1;
			return '?';
		}
		else
			optarg = argv[optind++];
		sp = 1;
	}
	else {
		if (argv[optind][++sp] == '\0') {
			sp = 1;
			optind++;
		}
		optarg = NULL;
	}
	return(c);
}
#endif
