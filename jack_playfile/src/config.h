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

//command line arguments
//========================================
static const char *filename=NULL; //the only mandatory argument

//start from absolute frame pos (skip n frames from start)
static uint64_t frame_offset=0;

//number of frames to read & play from offset (if argument not provided or 0: all frames)
static uint64_t frame_count=0; 

//if set to 0, keyboard entry won't be used (except ctrl+c)
//also no clock or other running information is displayed
static int keyboard_control_enabled=1;

//if set to 0, will not resample, even if file has different SR from JACK
static int use_resampling=1;

//if set to 1, connect available file channels to available physical outputs
static int autoconnect_jack_ports=1;

//if set to 1, try reconnect when JACK was temporarily down
//try to adapt to new JACK settings as good as possible
static int try_jack_reconnect=1;

//debug: connect to jalv.gtk http://gareus.org/oss/lv2/sisco#Stereo_gtk
static int connect_to_sisco=0;

//toggle play/pause with 'space'
//if set to 0: prepare everything for playing but wait for user to toggle to play
static int is_playing=1;

//status, not to be set as a configuration
//kind of pause but locked
static int is_idling_at_end=0;

//toggle mute with 'm'
static int is_muted=0;

//toggle loop with 'l'
static int loop_enabled=0;

//0: frames, 1: seconds
static int is_time_seconds=1;

//0: relative to frame_offset and frame_offset + frame_count
//1: relative to frame 0
static int is_time_absolute=0;

//0: time remaining (-), 1: time elapsed
static int is_time_elapsed=1;

//0: no running clock
static int is_clock_displayed=1;

//don't quit program when everything has played out
static int pause_at_end=0;

static const char *server_name = NULL; //default: 'default'
static const char *client_name = NULL; //default: 'jack_playfile'

//========================================

//if set to 1, will print stats
static int debug=0;

//if set to 1, will add "sample markers" for visual debugging in sisco
static int add_markers=0;
static float marker_first_sample_normal_jack_period=0.2;
static float marker_last_sample_out_of_resampler=-0.5;
static float marker_first_sample_last_jack_period=0.9;
static float marker_pad_samples_last_jack_period=-0.2;
static float marker_last_sample_last_jack_period=-0.9;

//for clock stepsize
//10^0=1 - 10^8=10000000
static int scale_exponent_frames=0;
static int scale_exponent_frames_min=0;
static int scale_exponent_frames_max=8;

//10-3=0.001 - 10^1=10, 2: 60 3: 600 4: 3600
static int scale_exponent_seconds=1;
static int scale_exponent_seconds_min=-3;
static int scale_exponent_seconds_max=4;

//jack_process() will return immediately if 0
static int process_enabled=0;
static int shutdown_in_progress=0;
static int shutdown_in_progress_signalled=0; //handled in loop in main()

//after connection to JACK, set to 0
//if jack shutdown handler called, set to 1
static int jack_server_down=1;

//arrows left and right, home, end etc
static int seek_frames_in_progress=0;

//don't operate on ringbuffers while reseting
//simple "lock"
static int reset_ringbuffers_in_progress=0;

//relative seek, how many (native) frames
uint64_t seek_frames_per_hit=0;

//relative seek, how many seconds
static double seek_seconds_per_hit=0;

//float, 4 bytes per sample
static int bytes_per_sample=4;//==sizeof(sample_t);

//set after connection to JACK succeeded
static int jack_sample_rate=0;
static int jack_period_frames=0;
static float jack_cycles_per_second=0;

//how many ports the JACK client will have
//for now: will use the same count as file has channels
static int output_port_count=0;

//JACK output bytes per second
static float jack_output_data_rate_bytes_per_second=0;

//JACK output to file input byte ratio
static float out_to_in_byte_ratio=0;

//counters
//JACK process cycles (not counted if process_enabled==0 or shutdown_in_progress==1)
//first cycle indicated as 1
static uint64_t process_cycle_count=0;

//counter for process() cycles where ringbuffer didn't have enough data for one cycle
//(one full period for every channel)
static uint64_t process_cycle_underruns=0;

static uint64_t total_frames_pushed_to_jack=0;

#endif
//EOF
