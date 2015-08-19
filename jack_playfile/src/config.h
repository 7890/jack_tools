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

#ifndef CONFIG_H_INC
#define CONFIG_H_INC

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

#include "weak_libjack.h"

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

#ifdef WIN32
        #include <windows.h>
        #define bzero(p, l) memset(p, 0, l)
#endif

#ifndef PRId64
	#define PRId64 "llu"
#endif

typedef struct 
{
	char *server_name;
	char *client_name;

	int process_enabled;

	int sample_rate;
	int period_frames; //JACK -p option
	float cycles_per_second;
	float output_data_rate_bytes_per_second;
	int server_down;
	uint64_t process_cycle_count;
	uint64_t process_cycle_underruns;
	uint64_t total_frames_pushed_to_jack;
	jack_options_t options;
	jack_status_t status;
	jack_transport_state_t transport_state;
	jack_port_t **ioPortArray;
	jack_client_t *client;

	int try_reconnect; //wait for JACK if not available, try reconnect if was shutdown
	int autoconnect_ports; //connect output ports to available physical system:playback ports 
	int use_transport; //set and follow JACK transport
} JackServer;

static JackServer *jack;

typedef struct
{
	float first_sample_normal_jack_period;
	float last_sample_out_of_resampler;
	float first_sample_last_jack_period;
	float pad_samples_last_jack_period;
	float last_sample_last_jack_period;
} DebugMarker;

static DebugMarker *debug_marker;

//readers will try to open this file
static const char *filename=NULL; 

//start from absolute frame pos (skip n frames from start)
static uint64_t frame_offset=0;

//remember offset given as argument
static uint64_t frame_offset_first=0;

//number of frames to read & play from offset (if argument not provided or 0: all frames)
static uint64_t frame_count=0; 

//remember count given as argument
static uint64_t frame_count_first=0; 

//relating to file channels
//start reading from offset (ignore n channels)
static int channel_offset=0;

//how many channels to read from offset (ignoring possible trailing channels)
static int channel_count=0;

//if set to 0, will not resample, even if file has different SR from JACK
static int use_resampling=1;

//override file sample rate to change pitch and tempo
static int custom_file_sample_rate=0;

//if set to 0: prepare everything for playing but wait for user to toggle to play
static int is_playing=1;

//toggle mute with 'm'
static int is_muted=0;

//toggle loop with 'l'
static int loop_enabled=0;

//don't quit program when everything has played out
static int pause_at_end=0;

//if set to 1, will print stats
static int debug=0;

//debug: connect to jalv.gtk http://gareus.org/oss/lv2/sisco#Stereo_gtk
static int connect_to_sisco=0;

//if set to 1, will add "sample markers" for visual debugging in sisco
static int add_markers=0;

//if set to one, disk thread will seek on next chance
static int seek_frames_in_progress=0;

//status, kind of pause but locked (forward actions input ignored)
static int is_idling_at_end=0;

//jack_process() will return immediately if 0
//static int process_enabled=0;
static int shutdown_in_progress=0;
static int shutdown_in_progress_signalled=0; //handled in loop in main()

//float, 4 bytes per sample
static int bytes_per_sample=4;//==sizeof(sample_t);

//how many ports the JACK client will have
//for now: will use the same count as file has channels
static int output_port_count=0;

//JACK output to file input byte ratio
static float out_to_in_byte_ratio=0;

//if set to 1, print more information while startup
static float is_verbose=0;

#endif
//EOF
