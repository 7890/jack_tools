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

#ifdef STATIC_BUILD
	#include "manpage.h"
#endif

//combine some files, order matters..
#include "config.h"
#include "sndin.h"
#include "control.h"
#include "kb_control.h"
#include "resampler.h"
#include "jackaudio.h"
#include "jack_playfile.h"

static const float version=0.84;

static void print_main_help();
static void print_manpage();
static void print_header();
static void print_version();
static void print_libs();

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",	no_argument,		0, 'h'},
	{"man",		no_argument,		0, 'H'},
	{"version",	no_argument,		0, 'V'},
	{"name",	required_argument,	0, 'n'},
	{"sname",	required_argument,	0, 's'},

	{"offset",	required_argument,	0, 'o'},
	{"count",	required_argument,	0, 'c'},

	{"choffset",	required_argument,	0, 'O'},
	{"chcount",	required_argument,	0, 'C'},

	{"nocontrol",	no_argument,  0,	'D'},
	{"noresampling",no_argument,  0,	'R'},
	{"noconnect",	no_argument,  0,	'N'},
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

	{"verbose",	no_argument,  0,	'v'},
	{"libs",	no_argument,  0,	'L'},

//{"",  no_argument,  &connect_to_sisco, 0},
	{0, 0, 0, 0}
};

//================================================================
static void print_main_help()
{
	fprintf (stderr, "\nUsage: jack_playfile [OPTION]... FILE...\n");
	fprintf (stderr, "Play FILEs through JACK.\n\n");

	fprintf (stderr, "  -h, --help                Display this text and quit\n");
	fprintf (stderr, "  -V, --version             Show program version and quit\n");
	fprintf (stderr, "  -n, --name <string>       JACK client name  (\"jack_playfile\") \n");
	fprintf (stderr, "  -s, --sname <string>      JACK server name  (\"default\") \n");
	fprintf (stderr, "  -N, --noconnect           Don't connect JACK ports\n");
	fprintf (stderr, "  -E, --noreconnect         Don't wait for JACK to re-connect\n");
	fprintf (stderr, "  -D, --nocontrol           Disable keyboard control\n");
	fprintf (stderr, "  -R, --noresampling        Disable resampling\n");
	fprintf (stderr, "  -p, --paused              Start paused\n");
	fprintf (stderr, "  -m, --muted               Start muted \n");
	fprintf (stderr, "  -l, --loop                Enable loop \n");
	fprintf (stderr, "  -e, --pae                 Pause at end or at start if --loop\n");
	fprintf (stderr, "  -j, --transport           Use JACK transport  (off)\n");
	fprintf (stderr, "  -f, --frames              Show time as frames  (vs. seconds) \n");
	fprintf (stderr, "  -a, --absolute            Show absolute time  (vs. relative) \n");
	fprintf (stderr, "  -r, --remaining           Show remaining time  (vs. elapsed) \n");
	fprintf (stderr, "  -k, --noclock             Disable clock display\n");
	fprintf (stderr, "  -o, --offset <integer>    Frame offset:  (0)\n");
	fprintf (stderr, "  -c, --count <integer>     Frame count:  (all)\n");
	fprintf (stderr, "  -O, --choffset <integer>  Channel offset:  (0)\n");
	fprintf (stderr, "  -C, --chcount <integer>   Channel count:  (all) \n\n");
	fprintf (stderr, "  -v, --verbose             Show more info about file, JACK\n");
	fprintf (stderr, "  -L, --libs                Show license and library info\n\n");

	fprintf (stderr, "Example: jack_playfile --remaining --count 44100 --loop music.opus\n");
	fprintf (stderr, "More infos in manual page. http://github.com/7890/jack_tools/\n\n");

#ifdef STATIC_BUILD
	fprintf (stderr, "This is a static build of jack_playfile.\n\n");
	fprintf (stderr, "Display a built-in manual page: jack_playfile --man\n\n");
#endif

	exit(0);
}

//=========================================================
static void print_manpage()
{
#ifdef STATIC_BUILD
	fprintf(stderr,"%s",jack_playfile_man_dump);
#endif
	exit(0);
}

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
#ifdef STATIC_BUILD
	fprintf (stderr, "\nThis is a static build of jack_playfile.\n");
	fprintf(stderr,"%s\n",build_info_dump);
#endif
}

//================================================================
static void print_libs()
{
	print_header();

	fprintf (stderr, "\nThis program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.\n\n");
	fprintf (stderr, "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.\n\n");
	fprintf (stderr, "You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.\n\n");

	fprintf (stderr, "Major audio libraries jack_playfile depends on:\n\n");

	fprintf (stderr, "JACK audio connection kit - http://jackaudio.org/\n");
	fprintf (stderr, "libsndfile - http://www.mega-nerd.com/libsndfile/\n");
	fprintf (stderr, "libzita-resampler - http://kokkinizita.linuxaudio.org/linuxaudio/\n");
	fprintf (stderr, "libopus, libopusfile - http://www.opus-codec.org/\n");
	fprintf (stderr, "libvorbisfile - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libmpg123 - http://www.mpg123.org/\n\n");

	fprintf (stderr, "libraries abstracted by libsndfile:\n");
	fprintf (stderr, "libFLAC - http://xiph.org/flac/\n");
	fprintf (stderr, "libvorbis, libvorbisenc - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libogg - http://xiph.org/ogg/\n\n");

	fprintf (stderr, "More infos in manpage. http://github.com/7890/jack_tools/\n\n");
}

#endif
//EOF
