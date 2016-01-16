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

#ifndef BUFFERS_H_INC
#define BUFFERS_H_INC

#include "rb.h"

//ringbuffers
//read from file, write to rb_interleaved (in case of resampling)
static rb_t *rb_interleaved=NULL;

//read from file, write (directly) to rb_resampled_interleaved (in case of no resampling), rb_interleaved unused/skipped
//read from rb_interleaved, resample and write to rb_resampled_interleaved (in case of resampling)
static rb_t *rb_resampled_interleaved=NULL;

//read from rb_resampled_interleaved, write to rb_deinterleaved
static rb_t *rb_deinterleaved=NULL;

//read from rb_deinterleaved, write to jack output buffers in JACK process()

//used for last few cycles
static rb_t *rb_resampler_swingout=NULL;

//don't operate on ringbuffers while reseting
//simple "lock"
static int reset_ringbuffers_in_progress=0;

static void setup_ringbuffers(int channel_count, int file_sample_rate, int jack_sample_rate);
static void free_ringbuffers();
static void reset_ringbuffers();

//=============================================================================
static void setup_ringbuffers(int channel_count, int file_sample_rate, int jack_sample_rate)
{
//	fprintf(stderr,"setup_ringbuffers()\n");
	rb_free(rb_interleaved);
	rb_free(rb_resampled_interleaved);
	rb_free(rb_deinterleaved);
	rb_free(rb_resampler_swingout);

	rb_interleaved                  =rb_new_audio_seconds(1.9,"interleaved",file_sample_rate,channel_count,sizeof(float));
	rb_resampled_interleaved        =rb_new_audio_seconds(1.9,"resampled interleaved",jack_sample_rate,channel_count,sizeof(float));
	rb_deinterleaved                =rb_new_audio_seconds(1.9,"deinterleaved",jack_sample_rate,channel_count,sizeof(float));
	rb_resampler_swingout		=rb_new_audio_seconds(0.5,"resampler swingout",jack_sample_rate,channel_count,sizeof(float));
}

//=============================================================================
static void free_ringbuffers()
{
//	fprintf(stderr,"free ringbuffers\n");
	if(rb_interleaved!=NULL)
	{
		rb_free(rb_interleaved);
	}

	if(rb_resampled_interleaved!=NULL)
	{
		rb_free(rb_resampled_interleaved);
	}

	if(rb_deinterleaved!=NULL)
	{
		rb_free(rb_deinterleaved);
	}

	if(rb_resampler_swingout!=NULL)
	{
		rb_free(rb_resampler_swingout);
	}
}

//=============================================================================
static void reset_ringbuffers()
{
//	fprintf(stderr,"reset ringbuffers\n");
	reset_ringbuffers_in_progress=1;

	if(rb_deinterleaved!=NULL)
	{
		rb_drop(rb_deinterleaved);
		rb_reset_stats(rb_deinterleaved);
	}

	if(rb_resampled_interleaved!=NULL)
	{
		rb_drop(rb_resampled_interleaved);
		rb_reset_stats(rb_resampled_interleaved);
	}

	if(rb_interleaved!=NULL)
	{
		rb_drop(rb_interleaved);
		rb_reset_stats(rb_interleaved);
	}

	if(rb_resampler_swingout!=NULL)
	{
		rb_drop(rb_resampler_swingout);
		rb_reset_stats(rb_resampler_swingout);
	}

	reset_ringbuffers_in_progress=0;
}

#endif
//EOF
