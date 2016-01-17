// ----------------------------------------------------------------------------
//
//  Copyright (C) 2015 - 2016 Thomas Brand <tom@trellis.ch>
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
//#include "rb.h"
//#include "buffers.h"
//#include "config.h"
//#include "resampler.h"
//#include "jack_playfile.h"
#include "jackaudio.h"
//#include "sndin.h"
//#include "playlist.h"
//#include "control.h"
#include "kb_control.h"

static const float version=0.90;

static void init_settings();
static void init_running_properties();
static void init_transport();
static void free_structs();

static void parse_cmdline_args(int argc, char *argv[]);

static void print_main_help();
static void print_manpage();
static void print_header();
static void print_version();
static void print_libs();

//================================================================
static void init_settings()
{
	settings=new Cmdline_Settings;

	settings->frame_offset=0;
	settings->frame_count=0; 
	settings->channel_offset=0;
	settings->channel_count=0;
//	settings->use_resampling=1;
	settings->resampler_filtersize=0; //use default
	settings->custom_file_sample_rate=0;
/*
	settings->is_playing=1;
	settings->is_muted=0;
	settings->loop_enabled=0;
	settings->pause_at_end=0;
*/
	settings->keyboard_control_enabled=1;
	settings->is_clock_displayed=1;
	settings->is_time_seconds=1;
	settings->is_time_absolute=0;
	settings->is_time_elapsed=1;
	settings->read_from_playlist=0;
	settings->dump_usable_files=0;
	settings->is_verbose=0;
	settings->debug=0;
	settings->connect_to_sisco=0;
	settings->add_markers=0;
}

//================================================================
static void init_running_properties()
{
	running=new Running_Properties;

	//copy from (initialized) settings
	running->frame_offset=settings->frame_offset;
	running->frame_count=settings->frame_count;
	running->channel_offset=settings->channel_offset;
	running->channel_count=settings->channel_count;
	running->seek_frames_in_progress=0;
/*
	running->is_idling_at_end=0;
*/
	running->shutdown_in_progress=0;
	running->shutdown_in_progress_signalled=0;
	running->out_to_in_byte_ratio=0;
	running->last_seek_pos=0;
}

//=============================================================================
static void init_transport()
{
	transport=new Transport();

	transport->is_playing=1;
	transport->is_muted=0;
	transport->loop_enabled=0;
	transport->pause_at_end=0;
	transport->is_idling_at_end=0;

	transport->use_jack_transport=0;
}

//================================================================
static void free_structs()
{
	delete settings;
	delete running;
	delete transport;
	free(jack->ioPortArray);
	delete jack;
	delete debug_marker;
	free(r1);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",	no_argument,		0, 'h'},
	{"man",		no_argument,		0, 'H'},
	{"version",	no_argument,		0, 'V'},
	{"name",	required_argument,	0, 'n'}, //JackServer
	{"sname",	required_argument,	0, 's'}, //JackServer
	{"ports",	required_argument,	0, 'w'}, //JackServer
	{"offset",	required_argument,	0, 'o'}, //Settings
	{"count",	required_argument,	0, 'c'}, //Settings
	{"choffset",	required_argument,	0, 'O'}, //Settings
	{"chcount",	required_argument,	0, 'C'}, //Settings
	{"samplerate",	required_argument,	0, 'S'}, //Settings
	{"amplify",	required_argument,	0, 'A'}, //JackServer
	{"file",	required_argument,	0, 'F'},
	{"dump",	no_argument,  0,	'd'},    //Settings
	{"nocontrol",	no_argument,  0,	'D'},    //Settings
/*	{"noresampling",no_argument,  0,	'R'},    //Settings */
	{"filtersize",	required_argument,	0, 'Q'}, //Settings
	{"noconnect",	no_argument,  0,	'N'},    //JackServer
	{"noreconnect",	no_argument,  0,	'E'},    //JackServer
	{"paused",	no_argument,  0,	'p'},    //Settings
	{"muted",	no_argument,  0,	'm'},    //Settings
	{"loop",	no_argument,  0,	'l'},    //Settings
	{"frames",	no_argument,  0,	'f'},    //Settings
	{"absolute",	no_argument,  0,	'a'},    //Settings
	{"remaining",	no_argument,  0,	'r'},    //Settings
	{"noclock",	no_argument,  0,	'k'},    //Settings
	{"pae",		no_argument,  0,	'e'},    //Settings
	{"transport",	no_argument,  0,	'j'},    //JackServer
	{"verbose",	no_argument,  0,	'v'},    //Settings
	{"libs",	no_argument,  0,	'L'},
	{0, 0, 0, 0}
};

//================================================================
static void parse_cmdline_args(int argc, char *argv[])
{
	int opt;
	//do until command line options parsed
	while(1)
	{
		//getopt_long stores the option index here
		int option_index=0;
		opt=getopt_long(argc, argv, "hHVn:s:w:o:c:O:C:DQ:S:A:F:dNEpmlfarkejvL", long_options, &option_index);

		//Detect the end of the options
		if(opt==-1)
		{
			break;
		}
		switch(opt)
		{
			case 0:

			//If this option set a flag, do nothing else now
			if(long_options[option_index].flag!=0)
			{
				break;
			}

			case 'h':
				print_main_help();
				break;

			case 'H':
				print_manpage();
				break;

			case 'V':
				print_version();
				exit(0);

			case 'n':
				jack->client_name=optarg;
				break;

			case 's':
				jack->server_name=optarg;
				break;

			case 'w':
				jack->output_port_count=atoi(optarg);
				break;

			case 'o':
				settings->frame_offset=strtoull(optarg, NULL, 10);
				break;

			case 'c':
				settings->frame_count=strtoull(optarg, NULL, 10);
				break;

			case 'O':
				settings->channel_offset=atoi(optarg);
				break;

			case 'C':
				settings->channel_count=atoi(optarg);
				break;

			case 'D':
				settings->keyboard_control_enabled=0;
				break;

//			case 'R':
//				settings->use_resampling=0;
//				break;

			case 'Q':
				settings->resampler_filtersize=atoi(optarg);
				break;

			case 'S':
				settings->custom_file_sample_rate=atoi(optarg);
				break;

			case 'A':
				jack->volume_amplification_decibel=MIN(6, atof(optarg) );
				jack->volume_coefficient=pow( 10, ( jack->volume_amplification_decibel / 20 ) );
				break;

			case 'F':
				settings->read_from_playlist=1;
				//in playlist.h
				playlist_file=optarg;
				break;

			case 'd':
				settings->dump_usable_files=1;
				break;

			case 'N':
				jack->autoconnect_ports=0;
				break;

			case 'E':
				jack->try_reconnect=0;
				break;

			case 'p':
				transport->is_playing=0;
				break;

			case 'm':
				transport->is_muted=1;
				break;

			case 'l':
				transport->loop_enabled=1;
				break;

			case 'f':
				settings->is_time_seconds=0;
				break;

			case 'a':
				settings->is_time_absolute=1;
				break;

			case 'r':
				settings->is_time_elapsed=0;
				break;

			case 'k':
				settings->is_clock_displayed=0;
				break;

			case 'e':
				transport->pause_at_end=1;
				break;

			case 'j':
				transport->use_jack_transport=1;
				break;

			case 'v':
				settings->is_verbose=1;
				break;

			case 'L':
				print_libs();
				exit(0);

			case '?': //invalid commands
				//getopt_long already printed an error message
				fprintf(stderr, "Wrong arguments, see --help.\n");
				exit(1);
				break;

			default:
				break;
		 } //end switch op
	}//end while(1) parse args
}//end parse_cmdline_args()

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

	fprintf (stdout, "  -w, --ports <integer>     Number of output ports (automatic)\n");

	fprintf (stdout, "  -N, --noconnect           Don't connect to default JACK ports\n");
	fprintf (stdout, "  -E, --noreconnect         Don't wait for JACK to re-connect\n");
	fprintf (stdout, "  -D, --nocontrol           Disable keyboard control\n");
//	fprintf (stdout, "  -R, --noresampling        Disable resampling\n");
	fprintf (stdout, "  -Q, --filtersize          Set custom resampler filtersize >=16,<=96)\n");
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
	fprintf (stdout, "  -O, --choffset <integer>  Channel offset (in file):  (0)\n");
	fprintf (stdout, "  -C, --chcount <integer>   Channel count  (in file):  (all)\n");
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
} //end print_main_help()

//=========================================================
static void print_manpage()
{
#ifdef STATIC_BUILD
	fprintf(stdout,"%s",jack_playfile_man_dump);
#else
	fprintf(stderr,"-H is only available in static builds of jack_playfile, please use 'man jack_playfile' instead.\n");
#endif
	exit(0);
}

//=========================================================
static void print_header()
{
	fprintf (stderr, "\njack_playfile v%.2f\n", version);
	fprintf (stderr, "(C) 2015 - 2016 Thomas Brand  <tom@trellis.ch>\n");
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
