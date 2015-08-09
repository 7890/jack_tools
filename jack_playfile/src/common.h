// ----------------------------------------------------------------------------
//
//  Copyright (C) 2015 Thomas Brand <tom@trellis.ch>
//    
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//  This file is part of jack_playfile
//  https://github.com/7890/jack_tools/
//
//tb/150612+
// ----------------------------------------------------------------------------

#ifndef COMMON_H_INC
#define COMMON_H_INC

//combine some files, order matters..
#include "config.h"
#include "sndin.h"
#include "control.h"
#include "resampler.h"
#include "jackaudio.h"
#include "jack_playfile.h"

//================================================================
static void print_main_help (void)
{
	fprintf (stderr, "Usage: jack_playfile [OPTION] FILE\n\n");

	fprintf (stderr, "  -h, --help              Display this text and quit\n");
	fprintf (stderr, "  -v, --version           Show program version and quit \n");
	fprintf (stderr, "  -n, --name <string>     JACK client name  (\"jack_playfile\") \n");
	fprintf (stderr, "  -s, --sname <string>    JACK server name  (\"default\") \n");
	fprintf (stderr, "  -C, --noconnect         Don't connect JACK ports\n");
	fprintf (stderr, "  -E, --noreconnect       Don't wait for JACK to re-connect\n");
	fprintf (stderr, "  -D, --nocontrol         Disable keyboard control\n");
	fprintf (stderr, "  -R, --noresampling      Disable resampling\n");
	fprintf (stderr, "  -p, --paused            Start paused\n");
	fprintf (stderr, "  -m, --muted             Start muted \n");
	fprintf (stderr, "  -l, --loop              Enable loop \n");
	fprintf (stderr, "  -e, --pae               Pause at end or at start if --loop\n");
	fprintf (stderr, "  -j, --transport         Use JACK transport  (off)\n");
	fprintf (stderr, "  -f, --frames            Show time as frames  (vs. seconds) \n");
	fprintf (stderr, "  -a, --absolute          Show absolute time  (vs. relative) \n");
	fprintf (stderr, "  -r, --remaining         Show remaining time  (vs. elapsed) \n");
	fprintf (stderr, "  -k, --noclock           Disable clock display\n");
	fprintf (stderr, "  -o, --offset <integer>  Frame offset:  (0)\n");
	fprintf (stderr, "  -c, --count <integer>   Frame count:  (all) \n\n");

//	fprintf (stderr, "  -L, --libs              Show library dependencies\n\n");

	fprintf (stderr, "Example: jack_playfile --remaining --count 44100 --loop music.opus\n");
	fprintf (stderr, "More infos in manpage. http://github.com/7890/jack_tools/\n\n");

	exit (0);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",	no_argument,		0, 'h'},
	{"version",	no_argument,		0, 'v'},
	{"name",	required_argument,	0, 'n'},
	{"sname",	required_argument,	0, 's'},

	{"offset",	required_argument,	0, 'o'},
	{"count",	required_argument,	0, 'c'},

	{"nocontrol",	no_argument,  0,	'D'},
	{"noresampling",no_argument,  0,	'R'},
	{"noconnect",	no_argument,  0,	'C'},
	{"noreconnect",	no_argument,  0,	'E'},
	{"paused",	no_argument,  0,	'p'},
	{"muted",	no_argument,  0,	'm'},
	{"loop",	no_argument,  0,	'l'},
	{"frames",	no_argument,  0,	'f'},
	{"absolute",	no_argument,  0,	'a'},
	{"remaining",	no_argument,  0,	'r'},
	{"noclock",	no_argument,  0,	'k'},
	{"pae",		no_argument,  0,	'e'},
	{"transport",	no_argument,  0,	'j'},

	{"libs",	no_argument,  0,	'L'},
//{"",  no_argument,  &connect_to_sisco, 0},
	{0, 0, 0, 0}
};

//=========================================================
static void print_header()
{
	fprintf (stderr, "\njack_playfile v%.2f\n", version);
	fprintf (stderr, "(C) 2015 Thomas Brand  <tom@trellis.ch>\n");
}

//=========================================================
static void print_version()
{
	fprintf (stderr, "%.2f\n",version);
}

#endif
//EOF
