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

#ifndef BUFFERS_H_INC
#define BUFFERS_H_INC

#include "config.h"

//ringbuffers
//read from file, write to rb_interleaved (in case of resampling)
static jack_ringbuffer_t *rb_interleaved=NULL;

//read from file, write (directly) to rb_resampled_interleaved (in case of no resampling), rb_interleaved unused/skipped
//read from rb_interleaved, resample and write to rb_resampled_interleaved (in case of resampling)
static jack_ringbuffer_t *rb_resampled_interleaved=NULL;

//read from rb_resampled_interleaved, write to rb_deinterleaved
static jack_ringbuffer_t *rb_deinterleaved=NULL;

//read from rb_deinterleaved, write to jack output buffers in JACK process()

static void setup_ringbuffers();
static void free_ringbuffers();
static void reset_ringbuffers();

//=============================================================================
static void setup_ringbuffers()
{
	///
	int size_multiplier=50;
	int rb_interleaved_size_bytes		=size_multiplier * sndfile_request_frames    * output_port_count * bytes_per_sample;
	int rb_resampled_interleaved_size_bytes	=size_multiplier * jack_period_frames        * output_port_count * bytes_per_sample;
	int rb_deinterleaved_size_bytes		=size_multiplier * jack_period_frames        * output_port_count * bytes_per_sample;

	rb_interleaved=jack_ringbuffer_create (rb_interleaved_size_bytes);
	rb_resampled_interleaved=jack_ringbuffer_create (rb_resampled_interleaved_size_bytes);
	rb_deinterleaved=jack_ringbuffer_create (rb_deinterleaved_size_bytes);

/*
	fprintf(stderr,"frames: request %d rb_interleaved %d rb_resampled_interleaved %d rb_deinterleaved %d\n"
		,sndfile_request_frames
		,rb_interleaved_size_bytes/output_port_count/bytes_per_sample
		,rb_resampled_interleaved_size_bytes/output_port_count/bytes_per_sample
		,rb_deinterleaved_size_bytes/output_port_count/bytes_per_sample
	);
*/

}

//=============================================================================
static void free_ringbuffers()
{
//	fprintf(stderr,"free ringbuffers\n");
	jack_ringbuffer_free(rb_interleaved);
	jack_ringbuffer_free(rb_resampled_interleaved);
	jack_ringbuffer_free(rb_deinterleaved);
}

//=============================================================================
static void reset_ringbuffers()
{
//	fprintf(stderr,"reset ringbuffers\n");
	jack_ringbuffer_reset(rb_deinterleaved);
	jack_ringbuffer_reset(rb_resampled_interleaved);
	jack_ringbuffer_reset(rb_interleaved);
}

#endif
//EOF
