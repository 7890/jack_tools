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

#ifndef CONTROL_H_INC
#define CONTROL_H_INC

//relative seek, how many (native) frames
uint64_t seek_frames_per_hit=0;

//relative seek, how many seconds
static double seek_seconds_per_hit=0;

//0: frames, 1: seconds
static int is_time_seconds=1;

//0: relative to frame_offset and frame_offset + frame_count
//1: relative to frame 0
static int is_time_absolute=0;

//0: time remaining (-), 1: time elapsed
static int is_time_elapsed=1;

//for clock stepsize
//10^0=1 - 10^8=10000000
static int scale_exponent_frames=0;
static int scale_exponent_frames_min=0;
static int scale_exponent_frames_max=8;

//10-3=0.001 - 10^1=10, 2: 60 3: 600 4: 3600
static int scale_exponent_seconds=1;
static int scale_exponent_seconds_min=-3;
static int scale_exponent_seconds_max=4;

static int ctrl_toggle_play();
static int ctrl_play();
static void ctrl_quit();
static void ctrl_seek_backward();
static int ctrl_seek_forward();
static void ctrl_seek_start();
static void ctrl_seek_start_play();
static void ctrl_seek_start_pause();
static int ctrl_seek_end();
static void ctrl_toggle_mute();
static void ctrl_toggle_loop();
static void ctrl_toggle_pause_at_end();
static void ctrl_toggle_jack_transport();
static void ctrl_jack_transport_on();
static void ctrl_jack_transport_off();

static void set_seconds_from_exponent();
static void set_frames_from_exponent();
static void increment_seek_step_size();
static void decrement_seek_step_size();

//=============================================================================
static void set_seconds_from_exponent()
{
	if(scale_exponent_seconds==2)
	{
		//60 seconds (1 minute)
		seek_seconds_per_hit=60;
	}
	else if(scale_exponent_seconds==3)
	{
		//600 seconds (10 minutes)
		seek_seconds_per_hit=600;
	}
	else if(scale_exponent_seconds==4)
	{
		//600 seconds (10 minutes)
		seek_seconds_per_hit=3600;
	}
	else
	{
		//10^exp seconds
		seek_seconds_per_hit=pow(10,scale_exponent_seconds);
	}

	seek_frames_per_hit=seek_seconds_per_hit*sf_info_generic.samplerate;
//	fprintf(stderr,"\nexp %d seek_seconds_per_hit %f\n",scale_exponent_seconds,seek_seconds_per_hit);
}

//=============================================================================
static void set_frames_from_exponent()
{
	seek_frames_per_hit=pow(10,scale_exponent_frames);
//	fprintf(stderr,"\nexp %d seek_frames_per_hit %"PRId64"\n",scale_exponent_frames,seek_frames_per_hit);
}

//=============================================================================
static void increment_seek_step_size()
{
	if(is_time_seconds)
	{
		scale_exponent_seconds++;
		scale_exponent_seconds=MIN(scale_exponent_seconds,scale_exponent_seconds_max);
		set_seconds_from_exponent();
	}
	else
	{
		scale_exponent_frames++;
		scale_exponent_frames=MIN(scale_exponent_frames,scale_exponent_frames_max);
		set_frames_from_exponent();
	}
}

//=============================================================================
static void decrement_seek_step_size()
{
	if(is_time_seconds)
	{
		scale_exponent_seconds--;
		scale_exponent_seconds=MAX(scale_exponent_seconds,scale_exponent_seconds_min);
		set_seconds_from_exponent();
	}
	else
	{
		scale_exponent_frames--;
		scale_exponent_frames=MAX(scale_exponent_frames,scale_exponent_frames_min);
		set_frames_from_exponent();
	}
}

#endif
//EOF
