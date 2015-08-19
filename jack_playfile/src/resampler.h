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

#ifndef resampler_H_INC
#define resampler_H_INC

#include <zita-resampler/resampler.h>

#include "buffers.h"

//zita-resampler
static Resampler R;

//quality (higher means better means more cpu)
//valid range 16 to 96
static int RESAMPLER_FILTERSIZE=64;

//JACK sr to file sr ratio
static double out_to_in_sr_ratio=1;

//frames put to resampler, without start/end padding
static uint64_t total_input_frames_resampled=0;

//in resample(), detect when all requested frames were resampled
static int resampling_finished=0;

//in jack_playfile.c
//static void print_stats();

/*
downsampling (ratio>1)
in: 10
out: 2
out_to_in: 2/10 = 0.2
read 10 samples for 2 output samples
read 0.2 samples for 1 output sample

upsampling (ratio<1)
in: 2
out: 10
out_to_in: 10/2 = 5
read two samples for 10 output samples
read 5 samples for 1 output sample

file 44100, jack 96000 -> 0.459375
for one jack_period_frames, we need at least period_size * out_to_in_sr_ratio frames from input
*/

//=============================================================================
static int get_resampler_pad_size_start()
{
	return R.inpsize()-1;
//	return R.inpsize()/2-1;
}

//=============================================================================
static int get_resampler_pad_size_end()
{
//	return R.inpsize()-1;
	return R.inpsize()/2-1;
}

//=============================================================================
static int setup_resampler()
{
	//test if resampling needed
	if(out_to_in_sr_ratio!=1)//sf_info_generic.samplerate!=jack_sample_rate)
	{
//		fprintf(stderr, "file sample rate different from JACK sample rate\n");

		if(use_resampling)
		{
			//prepare resampler for playback with given jack samplerate
			/*
			//http://kokkinizita.linuxaudio.org/linuxaudio/zita-resampler/resampler.html
			FILTSIZE: The valid range for hlen is 16 to 96.
			...it should be clear that 
			hlen = 32 should provide very high quality for F_min equal to 48 kHz or higher, 
			while hlen = 48 should be sufficient for an F_min of 44.1 kHz. 
			*/

			//setup returns zero on success, non-zero otherwise. 
			if (R.setup (sf_info_generic.samplerate, jack->sample_rate, channel_count_use_from_file, RESAMPLER_FILTERSIZE))
			{
				fprintf (stderr, "/!\\ sample rate ratio %d/%d is not supported.\n"
					,jack->sample_rate,sf_info_generic.samplerate);
				use_resampling=0;
				return 0;
			}
			else
			{
				/*
				The inpsize () member returns the lenght of the FIR filter expressed in input samples. 
				At least this number of samples is required to produce an output sample.

				inserting inpsize() / 2 - 1 zero-valued samples at the start will align the first input and output samples.
				inserting k - 1 zero valued samples will ensure that the output includes the full filter response for the first input sample.
				*/
				//initialize resampler
				R.reset();

				R.inp_data=0;
////////////////////
				R.inp_count=get_resampler_pad_size_start();
				//pad with zero
				R.out_data=0;
				R.out_count=1;
				R.process();
/*
				fprintf(stderr,"resampler init: inp_count %d out_count %d\n",R.inp_count,R.out_count);
				fprintf (stderr, "resampler initialized: inpsize() %d inpdist() %.2f sr in %d sr out %d out/in ratio %f\n"
					,R.inpsize()
					,R.inpdist()
					,sf_info_generic.samplerate
					,jack->sample_rate
					,out_to_in_sr_ratio);
*/

				if(is_verbose)
				{
					fprintf(stderr,"resampler out_to_in ratio: %f\n",out_to_in_sr_ratio);
				}

			}//end resampler setup
 		}//end if use_resampling
		else
		{
			fprintf(stderr,"will play file without resampling.\n");
		}
	}//end unequal in/out sr

	return 1;

}//end setup_resampler()

//=============================================================================
static void resample()
{
	if(out_to_in_sr_ratio==1.0 || !use_resampling || resampling_finished)
	{
		resampling_finished=1;

		//no need to do anything
		return;
	}

//	fprintf(stderr,"resample() called\n");

	//normal operation
	if(jack_ringbuffer_read_space(rb_interleaved) 
		>= sndfile_request_frames * channel_count_use_from_file * bytes_per_sample)
	{
//		fprintf(stderr,"resample(): normal operation\n");

		float *interleaved_frame_buffer=new float [sndfile_request_frames * channel_count_use_from_file];
		float *buffer_resampling_out=new float [jack->period_frames * channel_count_use_from_file];

		//condition to jump into while loop
		R.out_count=1;

		int resampler_loop_counter=0;
		while(R.out_count>0)
		{
			//read from rb_interleaved, just peek / don't move read pointer yet
			jack_ringbuffer_peek(rb_interleaved
				,(char*)interleaved_frame_buffer
				,sndfile_request_frames * channel_count_use_from_file * bytes_per_sample);
			
			//configure for next resampler process cycle
			R.inp_data=interleaved_frame_buffer;
			R.inp_count=sndfile_request_frames;
			R.out_data=buffer_resampling_out;
			R.out_count=jack->period_frames;

//			fprintf(stderr,"--- resample(): before inpcount %d outcount %d\n",R.inp_count,R.out_count);
			R.process();
//			fprintf(stderr,"--- resample(): after inpcount %d outcount %d loop %d\n",R.inp_count,R.out_count,resampler_loop_counter);

			if(R.inp_count>0)
			{
//				fprintf(stderr,"resample(): /!\\ after process r.inp_count %d\n",R.inp_count);
				//this probably means that the remaining input sample is not yet processed to out
				//we'll use it again for the next cycle (feed as the first sample of the next processing block)
			}

			//advance - remaining inp_count!
			jack_ringbuffer_read_advance(rb_interleaved
				,(sndfile_request_frames-R.inp_count) * channel_count_use_from_file * bytes_per_sample);

			total_input_frames_resampled+=(sndfile_request_frames-R.inp_count);

			resampler_loop_counter++;
		}//end while(R.out_count>0)

		//finally write resampler output to rb_resampled_interleaved
		jack_ringbuffer_write(rb_resampled_interleaved
			,(const char*)buffer_resampling_out
			,jack->period_frames * channel_count_use_from_file * bytes_per_sample);

		delete[] interleaved_frame_buffer;
		delete[] buffer_resampling_out;
	}

	//finished with partial or no data left, feed zeroes at end
	else if(all_frames_read && jack_ringbuffer_read_space(rb_interleaved)>=0)
	{
		int frames_left=jack_ringbuffer_read_space(rb_interleaved)/channel_count_use_from_file/bytes_per_sample;
//		fprintf(stderr,"resample(): partial data in rb_interleaved (frames): %d\n",frames_left);

		//adding zero pad to get full output of resampler
////////////////////
		int final_frames=frames_left + get_resampler_pad_size_end();

		float *interleaved_frame_buffer=new float [ final_frames  * channel_count_use_from_file];
		float *buffer_resampling_out=new float [ ( (int)(out_to_in_sr_ratio * final_frames) ) * channel_count_use_from_file ];

		//read from rb_interleaved
		jack_ringbuffer_read(rb_interleaved
			,(char*)interleaved_frame_buffer
			,frames_left * channel_count_use_from_file * bytes_per_sample);
			
		//configure resampler for next process cycle
		R.inp_data=interleaved_frame_buffer;
		R.inp_count=final_frames;

		R.out_data=buffer_resampling_out;
		R.out_count=(int) (out_to_in_sr_ratio * final_frames);

//		fprintf(stderr,"LAST before inpcount %d outcount %d\n",R.inp_count,R.out_count);
		R.process();
//		fprintf(stderr,"LAST after inpcount %d outcount %d\n",R.inp_count,R.out_count);

		//don't count zero padding frames
		total_input_frames_resampled+=frames_left;

		if(add_markers)
		{
			//index of last sample of first channel
			int last_sample_index=( (int)(out_to_in_sr_ratio * final_frames) ) * channel_count_use_from_file - channel_count_use_from_file;

			//mark last samples of all channels
			for(int i=0;i<channel_count_use_from_file;i++)
			{
				buffer_resampling_out[last_sample_index+i]  =debug_marker->last_sample_out_of_resampler;
			}
		}

		//finally write resampler output to rb_resampled_interleaved
		jack_ringbuffer_write(rb_resampled_interleaved
			,(const char*)buffer_resampling_out
			,(int)(out_to_in_sr_ratio * final_frames) * channel_count_use_from_file * bytes_per_sample);

		resampling_finished=1;
	}
	else
	{
//		fprintf(stderr,"/!\\ this should not happen\n");
	}

//	print_stats();

}//end resample()

#endif
//EOF
