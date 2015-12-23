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
#include "rb.h"
#include "sndin.h"
#include "kb_control.h"
#include "resampler.h"
#include "jackaudio.h"
#include "playlist.h"

static const float version=0.88;

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

	{"samplerate",	required_argument,	0, 'S'},

	{"amplify",	required_argument,	0, 'A'},

	{"file",	required_argument,	0, 'F'},
	{"dump",	no_argument,  0,	'd'},

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
	fprintf (stdout, "\nUsage: jack_playfile [OPTION]... FILE...\n");
	fprintf (stdout, "Play FILEs through JACK.\n\n");

	fprintf (stdout, "  -h, --help                Display this text and quit\n");
	fprintf (stdout, "  -V, --version             Show program version and quit\n");
	fprintf (stdout, "  -F, --file <string>       Get files to play from playlist file\n");
	fprintf (stdout, "  -n, --name <string>       JACK client name  (\"jack_playfile\") \n");
	fprintf (stdout, "  -s, --sname <string>      JACK server name  (\"default\") \n");
	fprintf (stdout, "  -N, --noconnect           Don't connect JACK ports\n");
	fprintf (stdout, "  -E, --noreconnect         Don't wait for JACK to re-connect\n");
	fprintf (stdout, "  -D, --nocontrol           Disable keyboard control\n");
	fprintf (stdout, "  -R, --noresampling        Disable resampling\n");
	fprintf (stdout, "  -S, --samplerate          Override file sample rate (affects pitch & tempo)\n");
	fprintf (stdout, "  -A, --amplify             Amplifcation in dB (Volume):  (0.0)\n");
	fprintf (stdout, "  -p, --paused              Start paused\n");
	fprintf (stdout, "  -m, --muted               Start muted \n");
	fprintf (stdout, "  -l, --loop                Enable loop \n");
	fprintf (stdout, "  -e, --pae                 Pause at end or at start if --loop\n");
	fprintf (stdout, "  -j, --transport           Use JACK transport  (off)\n");
	fprintf (stdout, "  -f, --frames              Show time as frames  (vs. seconds) \n");
	fprintf (stdout, "  -a, --absolute            Show absolute time  (vs. relative) \n");
	fprintf (stdout, "  -r, --remaining           Show remaining time  (vs. elapsed) \n");
	fprintf (stdout, "  -k, --noclock             Disable clock display\n");
	fprintf (stdout, "  -o, --offset <integer>    Frame offset:  (0)\n");
	fprintf (stdout, "  -c, --count <integer>     Frame count:  (all)\n");
	fprintf (stdout, "  -O, --choffset <integer>  Channel offset:  (0)\n");
	fprintf (stdout, "  -C, --chcount <integer>   Channel count:  (all)\n");
	fprintf (stdout, "  -d, --dump                Print usable files to stdout and quit\n");
	fprintf (stdout, "  -v, --verbose             Show more info about files, JACK settings\n");
	fprintf (stdout, "  -L, --libs                Show license and library info\n\n");

	fprintf (stdout, "Example: jack_playfile song.wav\n");
	fprintf (stdout, "More infos in manual page. http://github.com/7890/jack_tools/\n\n");

#ifdef STATIC_BUILD
	fprintf (stdout, "This is a static build of jack_playfile.\n\n");
	fprintf (stdout, "Display a built-in manual page: jack_playfile --man|less\n\n");
#endif

	exit(0);
}

//=========================================================
static void print_manpage()
{
#ifdef STATIC_BUILD
	fprintf(stdout,"%s",jack_playfile_man_dump);
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
	fprintf (stdout, "%.2f\n",version);
#ifdef STATIC_BUILD
	fprintf (stdout, "\nThis is a static build of jack_playfile.\n");
	fprintf(stdout,"%s\n",build_info_dump);
#endif
}

//================================================================
static void print_libs()
{
	print_header();

	fprintf (stdout, "\nThis program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.\n\n");
	fprintf (stdout, "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.\n\n");
	fprintf (stdout, "You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.\n\n");

	fprintf (stdout, "Major audio libraries jack_playfile depends on:\n\n");

	fprintf (stdout, "JACK audio connection kit - http://jackaudio.org/\n");
	fprintf (stdout, "libsndfile - http://www.mega-nerd.com/libsndfile/\n");
	fprintf (stdout, "libzita-resampler - http://kokkinizita.linuxaudio.org/linuxaudio/\n");
	fprintf (stdout, "libopus, libopusfile - http://www.opus-codec.org/\n");
	fprintf (stdout, "libvorbisfile - http://xiph.org/vorbis/\n");
	fprintf (stdout, "libmpg123 - http://www.mpg123.org/\n\n");

	fprintf (stdout, "libraries abstracted by libsndfile:\n");
	fprintf (stdout, "libFLAC - http://xiph.org/flac/\n");
	fprintf (stdout, "libvorbis, libvorbisenc - http://xiph.org/vorbis/\n");
	fprintf (stdout, "libogg - http://xiph.org/ogg/\n\n");

	fprintf (stdout, "More infos in manpage. http://github.com/7890/jack_tools/\n\n");
}

#endif
//EOF
