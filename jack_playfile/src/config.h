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

#ifndef CONFIG_H_INC
#define CONFIG_H_INC

#include <stdio.h>
#include <unistd.h>
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

//=============================================================================
typedef struct 
{
	char *server_name;
	char *client_name;

	int sample_rate;
	int period_frames; //JACK -p option
	double period_duration; //calc
	double cycles_per_second; //calc
	double output_data_rate_bytes_per_second; //calc

	int process_enabled;
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

	//how many ports the JACK client will have. fixed count if given as argument
	//derived from first file in playlist (# of channels) and offset if not explicitely given
	int output_port_count; //jack client will have that many output ports

	float volume_coefficient; //0 - 2
	float volume_amplification_decibel; //-INF - 6 dBFS
	int clipping_detected; //if sample value >=1, checked in jack_playfile.c deinterleave()
} JackServer;

static JackServer *jack;

//=============================================================================
typedef struct
{
	float first_sample_normal_jack_period;
	float last_sample_out_of_resampler;
	float first_sample_last_jack_period;
	float pad_samples_last_jack_period;
	float last_sample_last_jack_period;
} DebugMarker;

static DebugMarker *debug_marker;

//startup options
//=============================================================================
typedef struct
{
	//some values will need to be adjusted to fit the currently loaded file in a playlist
	//however the settings are kept as given on startup in order to try to apply to a next file in the playlist

	uint64_t frame_offset; //start from absolute frame pos (skip n frames from start)
	uint64_t frame_count; //number of frames to read & play from offset (if argument not provided or 0: all frames)

	int channel_offset; //start reading from offset (ignore n channels), relating to file channels
	int channel_count; //how many channels to read from offset (ignoring possible trailing channels)

//	int use_resampling; //if set to 0, will not resample, even if file has different SR from JACK

	int resampler_filtersize; //filtersize (~quality) to use when setting up zita-resampler >=16, <=96, default 64

	int custom_file_sample_rate; //override file sample rate to change pitch and tempo

	int keyboard_control_enabled; //if set to 0, keyboard entry won't be used (except ctrl+c). also no clock or other running information is displayed.
	int is_clock_displayed; //0: no running clock
	int is_time_seconds; //0: frames, 1: seconds
	int is_time_absolute; //0: relative to frame_offset and frame_offset + frame_count. 1: relative to frame 0.
	int is_time_elapsed; //0: time remaining (-), 1: time elapsed

	int read_from_playlist; //if set to 1, get files to play from given file, one song per line
	int read_recursively;
	int dump_usable_files; //if set to 1, dump usable files in playlist or args to stdout and quit

	float is_verbose; //if set to 1, print more information while starting up
	int debug; //if set to 1, will print stats
	int connect_to_sisco; //debug: connect to jalv.gtk http://gareus.org/oss/lv2/sisco#Stereo_gtk

	int add_markers; //if set to 1, will add "sample markers" for visual debugging in sisco
} Cmdline_Settings;

static Cmdline_Settings *settings;

//running properties
//=============================================================================
typedef struct
{
	//the initial values are copied from Cmdline_Options struct and adapted to currently loaded file
	//some values might be adjusted/limited

	uint64_t frame_offset; //start from absolute frame pos (skip n frames from start)
	uint64_t frame_count; //number of frames to read & play from offset (if argument not provided or 0: all (remaining) frames)

	int channel_offset; //start reading from offset (ignore n channels), relating to file channels
	int channel_count; //how many channels to read from offset (if argument not provided or 0: all (remaining) channels)

	int seek_frames_in_progress; //if set to one, disk thread will seek on next chance

	int shutdown_in_progress;
	int shutdown_in_progress_signalled;

	float out_to_in_byte_ratio; //JACK output to file input byte ratio

	uint64_t last_seek_pos; //set file frame position on every seek
} Running_Properties;

static Running_Properties *running;

//transport
//=============================================================================
typedef struct
{
	int is_playing; //if set to 0: prepare everything for playing but wait for user to toggle to play
	int is_muted;
	int loop_enabled;
	int pause_at_end; //don't quit program when everything has played out
	int is_idling_at_end; //status, kind of pause but locked (forward actions input ignored)
	int use_jack_transport; //set and follow JACK transport
} Transport;

static Transport *transport;

static const char *cisco_in1="Simple Scope (Stereo):in1";
static const char *cisco_in2="Simple Scope (Stereo):in2";
//static const char *cisco_in1="Simple Scope (Stereo) GTK:in1";
//static const char *cisco_in2="Simple Scope (Stereo) GTK:in2";
//static const char *cisco_in1="Simple Scope (3 channel) GTK:in1";
//static const char *cisco_in2="Simple Scope (3 channel) GTK:in2";

//readers will try to open this file
static const char *filename=NULL; 

//float, 4 bytes per sample
static int bytes_per_sample=sizeof(float);

///
static const char *clear_to_eol_seq=NULL;
static const char *turn_on_cursor_seq=NULL;
static const char *turn_off_cursor_seq=NULL;

#endif
//EOF
