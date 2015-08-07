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
	fprintf (stderr, "Usage: jack_playfile [Options] audiofile\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text and quit          --help\n");
	fprintf (stderr, "  Show program version and quit       --version\n");
	fprintf (stderr, "  JACK client name    (jack_playfile) --name <string>\n");
	fprintf (stderr, "  JACK server name          (default) --sname <string>\n");
	fprintf (stderr, "  Don't connect JACK ports            --noconnect\n");
	fprintf (stderr, "  Don't wait for JACK to re-connect   --noreconnect\n");
	fprintf (stderr, "  Disable keyboard control            --nocontrol\n");
	fprintf (stderr, "  Disable resampling                  --noresampling\n");
	fprintf (stderr, "  Start paused                        --paused\n");
	fprintf (stderr, "  Start muted                         --muted\n");
	fprintf (stderr, "  Enable loop                         --loop\n");
	fprintf (stderr, "  Pause at end (at start if --loop)   --pae\n");
	fprintf (stderr, "  Show time as frames       (seconds) --frames\n");
	fprintf (stderr, "  Show absolute time            (rel) --absolute\n");
	fprintf (stderr, "  Show remaining time       (elapsed) --remaining\n");
	fprintf (stderr, "  Disable clock display               --noclock\n");
	fprintf (stderr, "  Frame offset:                   (0) --offset <integer>\n");
	fprintf (stderr, "  Frame count:                  (all) --count <integer>\n\n");

	fprintf (stderr, "Example: jack_playfile --remaining --count 44100 --loop music.opus\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools/\n\n");

	fprintf (stderr, "jack_playfile is free software.\n");
	fprintf (stderr, "Major audio libraries jack_playfile depends on:\n\n");

	fprintf (stderr, "JACK audio connection kit - http://jackaudio.org/\n");
	fprintf (stderr, "libsndfile - http://www.mega-nerd.com/libsndfile/\n");
	fprintf (stderr, "libzita-resampler - http://kokkinizita.linuxaudio.org/linuxaudio/\n");
	fprintf (stderr, "libopus, libopusfile - http://www.opus-codec.org/\n");
	fprintf (stderr, "libvorbisfile - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libmpg123 - http://www.mpg123.de/ (optional due to patent foo)\n\n");

	fprintf (stderr, "libraries abstracted by libsndfile:\n\n");
	fprintf (stderr, "libFLAC - http://xiph.org/flac/\n");
	fprintf (stderr, "libvorbis, libvorbisenc - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libogg - http://xiph.org/ogg/\n\n");

	exit (0);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",	no_argument,		0, 'a'},
	{"version",	no_argument,		0, 'b'},
	{"name",	required_argument,	0, 'c'},
	{"sname",	required_argument,	0, 'd'},

	{"offset",	required_argument,	0, 'e'},
	{"count",	required_argument,	0, 'f'},

	{"nocontrol",	no_argument,  &keyboard_control_enabled,0},
	{"noresampling",no_argument,  &use_resampling,		0},
	{"noconnect",	no_argument,  &autoconnect_jack_ports,	0},
	{"noreconnect",	no_argument,  &try_jack_reconnect,	0},
	{"paused",	no_argument,  &is_playing,		0},
	{"muted",	no_argument,  &is_muted,		1},
	{"loop",	no_argument,  &loop_enabled,		1},
	{"frames",	no_argument,  &is_time_seconds,		0},
	{"absolute",	no_argument,  &is_time_absolute,	1},
	{"remaining",	no_argument,  &is_time_elapsed,		0},
	{"noclock",	no_argument,  &is_clock_displayed,	0},
	{"pae",		no_argument,  &pause_at_end,	1},
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
