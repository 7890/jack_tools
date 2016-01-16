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

#ifndef CONTROL_H_INC
#define CONTROL_H_INC

#include "jack_playfile.h"
#include "playlist.h"

//signal wether to go to previous or next index in playlist
static int playlist_advance_direction=PL_DIRECTION_FORWARD;

//relative seek, how many (native) frames
uint64_t seek_frames_per_hit=0;

//relative seek, how many seconds
static double seek_seconds_per_hit=0;

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
static void ctrl_load_prev_file();
static void ctrl_load_next_file();

static void ctrl_decrement_volume();
static void ctrl_increment_volume();
static void ctrl_reset_volume();

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

	seek_frames_per_hit=seek_seconds_per_hit*sf_info_generic.sample_rate;
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
	if(settings->is_time_seconds)
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
	if(settings->is_time_seconds)
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

//0: pause, 1: play, 2: idling
//=============================================================================
static int ctrl_toggle_play()
{
	if(transport->pause_at_end && transport->is_idling_at_end)
	{
		return 2;
	}
	else
	{
		transport->is_idling_at_end=0;
		transport->is_playing=!transport->is_playing;
		if(transport->use_jack_transport)
		{
			if(transport->is_playing)
			{
				jack_transport_start(jack->client);
			}
			else
			{
				jack_transport_stop(jack->client);
			}
		}

		return transport->is_playing;
	}
}

//=============================================================================
static int ctrl_play()
{
	if(transport->pause_at_end && transport->is_idling_at_end)
	{
		return 2;
	}
	else
	{
		int tmp=0;
		transport->is_idling_at_end=0;
		transport->is_playing=1;
		if(transport->use_jack_transport)
		{
			jack_transport_start(jack->client);
		}
		return transport->is_playing;
	}
}

//=============================================================================
static void ctrl_pause()
{
	///here: explicitly request pause (not via toggle)
}

//=============================================================================
static void ctrl_quit()
{
	no_more_files_to_play=1;
	transport->loop_enabled=0; //prepare seek
	transport->pause_at_end=0;
	transport->is_idling_at_end=0;
	transport->is_playing=1;///
	ctrl_seek_end(); //seek to end ensures zeroed buffers (while seeking)
}

//=============================================================================
static void ctrl_seek_backward()
{
	seek_frames(-seek_frames_per_hit);
}

//=============================================================================
static int ctrl_seek_forward()
{
	if(transport->pause_at_end && transport->is_idling_at_end)
	{
		return 2;
	}
	else
	{
		transport->is_idling_at_end=0;
		fprintf(stderr,">> ");
		seek_frames( seek_frames_per_hit);
		return transport->is_playing;
	}
}

//set seek step size

//=============================================================================
static void ctrl_seek_start()
{
	seek_frames_absolute(running->frame_offset);
}

//=============================================================================
static void ctrl_seek_start_play()
{
	transport->is_playing=0;
	seek_frames_absolute(running->frame_offset);
	transport->is_playing=1;

	if(transport->use_jack_transport)
	{
		jack_transport_start(jack->client);
	}
}

//=============================================================================
static void ctrl_seek_start_pause()
{
	seek_frames_absolute(running->frame_offset);
}

//=============================================================================
static int ctrl_seek_end()
{
	if(transport->is_idling_at_end)
	{
		return 2;
	}
	else
	{
		fprintf(stderr,">| end ");
		seek_frames_absolute(running->frame_offset+running->frame_count);
		return transport->is_playing;
	}
}

//for all toggles: also need fixed set

//=============================================================================
static void ctrl_toggle_mute()
{
	transport->is_muted=!transport->is_muted;
}

//=============================================================================
static void ctrl_toggle_loop()
{
	transport->loop_enabled=!transport->loop_enabled;

	if(transport->loop_enabled && all_frames_read())
	{
		seek_frames_absolute(running->frame_offset);
	}
}

//=============================================================================
static void ctrl_toggle_pause_at_end()
{
	transport->pause_at_end=!transport->pause_at_end;
	if(transport->pause_at_end && all_frames_read())
	{
		transport->is_idling_at_end=1;
	}
}

//=============================================================================
static void ctrl_toggle_jack_transport()
{
	transport->use_jack_transport=!transport->use_jack_transport;
}

//=============================================================================
static void ctrl_jack_transport_on()
{
	transport->use_jack_transport=1;
}

//=============================================================================
static void ctrl_jack_transport_off()
{
	transport->use_jack_transport=0;
}

//=============================================================================
static void ctrl_load_prev_file()
{
	playlist_advance_direction=PL_DIRECTION_BACKWARD;
	transport->loop_enabled=0; //prepare seek
	transport->pause_at_end=0;
	transport->is_idling_at_end=0;
	transport->is_playing=1;///
	ctrl_seek_end(); //seek to end ensures zeroed buffers (while seeking)
}

//=============================================================================
static void ctrl_load_next_file()
{
	transport->loop_enabled=0; //prepare seek
	transport->pause_at_end=0;
	transport->is_idling_at_end=0;
	transport->is_playing=1;///
	ctrl_seek_end(); //seek to end ensures zeroed buffers (while seeking)
}

/*
Equations in log base 10:
linear-to-db(x) = log(x) * 20
db-to-linear(x) = 10^(x / 20)
*/
//=============================================================================
static void ctrl_decrement_volume()
{
	float db_step=0;
	float db_amp=jack->volume_amplification_decibel;

	if(db_amp <= -120)
	{
		db_amp=-INFINITY;
	}

	else if(db_amp <= -60)
	{
		db_amp-=6;
	}
	else if(db_amp <= -18)
	{
		db_amp-=1;
	}
	else
	{
		db_amp-=0.5;
	}
	jack->volume_amplification_decibel=db_amp;
	jack->volume_coefficient=pow( 10, ( jack->volume_amplification_decibel / 20 ) );
	//fprintf(stderr,"\ncoeff: %f amp db: %f\n",jack->volume_coefficient,jack->volume_amplification_decibel);
}

//=============================================================================
static void ctrl_increment_volume()
{
	float db_step=0;
	float db_amp=jack->volume_amplification_decibel;

	if(db_amp < -120)
	{
		db_amp=-120;
	}
	else if(db_amp < -60)
	{
		db_amp+=6;
	}
	else if(db_amp < -18)
	{
		db_amp+=1;
	}
	else
	{
		db_amp+=0.5;
	}
	jack->volume_amplification_decibel=MIN(6,db_amp);
	jack->volume_coefficient=pow( 10, ( jack->volume_amplification_decibel / 20 ) );
	//fprintf(stderr,"\ncoeff: %f amp db: %f\n",jack->volume_coefficient,jack->volume_amplification_decibel);
}

//=============================================================================
static void ctrl_reset_volume()
{
	jack->volume_amplification_decibel=0;
	jack->volume_coefficient=1;
	//fprintf(stderr,"\ncoeff: %f amp db: %f\n",jack->volume_coefficient,jack->volume_amplification_decibel);
}

#endif
//EOF
