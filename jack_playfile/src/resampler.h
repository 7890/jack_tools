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

#ifndef resampler_H_INC
#define resampler_H_INC

#include <zita-resampler/resampler.h>

//#include "rb.h"
#include "buffers.h"

//main zita-resampler
Resampler R;

typedef struct
{
	//quality (higher means better means more cpu use)
	//valid range 16 to 96
	int filtersize;

	rb_t *in_buffer;
	rb_t *out_buffer;

	//JACK sr to file sr ratio
	double out_to_in_sr_ratio;

	int input_period_frames;
	int output_period_frames;

	//frames put to resampler, without start/end padding
	size_t total_input_frames_resampled;

	//in rs_process(), detect when all requested frames were resampled
	int resampling_finished;
} rs_t;

//=============================================================================
static int rs_get_pad_size_start(rs_t *r)
{
	return R.inpsize()-1;
//	return R.inpsize()/2-1;
}

//=============================================================================
static int rs_get_pad_size_end(rs_t *r)
{
//	return R.inpsize()-1;
	return R.inpsize()/2-1;
}

//=============================================================================
static void rs_flush(rs_t *r)///
{
	R.out_count=1;
	while(R.out_count>0)
	{
//		fprintf(stderr,"\nFLUSH\n");
		R.inp_data=0;
		R.inp_count=100000;
		//pad with zero
		R.out_data=0;
		R.out_count=1000;//3*R.inpsize();
		R.process();
	}
}

//=============================================================================
static void rs_free(rs_t *r)
{
	if(r==NULL) {return;}
	free(r);
}

//=============================================================================
static rs_t * rs_new(int quality, rb_t *in_buffer, rb_t *out_buffer, int output_period_frames)
{
	if(in_buffer==NULL || out_buffer==NULL) {return NULL;}

	//need audio aware rb_t
	if(!rb_sample_rate(in_buffer) || !rb_sample_rate(out_buffer)) {return NULL;}

	rs_t *r;
	r=(rs_t*)malloc(sizeof(rs_t));
	if(r==NULL) {return NULL;}

	if(quality==0)
	{
		quality=64;///default
	}

	r->out_to_in_sr_ratio=(float)rb_sample_rate(out_buffer)/rb_sample_rate(in_buffer);

	r->filtersize=MAX(MIN(quality,96),16); //limit 16,96
	r->in_buffer=in_buffer;
	r->out_buffer=out_buffer;
	r->output_period_frames=output_period_frames;

	r->total_input_frames_resampled=0;
	r->resampling_finished=0;

	r->input_period_frames=ceil(r->output_period_frames * (double)1/r->out_to_in_sr_ratio);

	//setup returns zero on success, non-zero otherwise. 
	if (R.setup (rb_sample_rate(r->in_buffer), rb_sample_rate(r->out_buffer), rb_channel_count(r->in_buffer), r->filtersize))
	{
		fprintf (stderr, "/!\\ sample rate ratio %d/%d is not supported.\n"
			,rb_sample_rate(r->out_buffer)
			,rb_sample_rate(r->in_buffer)
		);
		free(r);
		return NULL;
	}

	//initialize resampler
	//R.reset();
	R.inp_data=0;
	R.inp_count=rs_get_pad_size_start(r);
	//pad with zero
	R.out_data=0;
	R.out_count=1;
	R.process();

/*
	fprintf(stderr, "resampler initialized: inpsize() %d inpdist() %.2f sr in %d sr out %d out/in ratio %f\n"
		,R.inpsize()
		,R.inpdist()
		,rb_sample_rate(r->in_buffer)
		,rb_sample_rate(r->out_buffer)
		,r->out_to_in_sr_ratio);
*/
	return r;
}//end rs_new()

//=============================================================================
static int rs_process(rs_t *r)
{
	if(r->out_to_in_sr_ratio==1.0 || r->resampling_finished)
	{
		r->resampling_finished=1;
		//no need to do anything
		return 0;
	}
//	fprintf(stderr,"rs_process() called\n");

	int float_count_in =r->input_period_frames  * rb_channel_count(r->in_buffer);
	int float_count_out=r->output_period_frames * rb_channel_count(r->in_buffer);

	int byte_count_in =float_count_in  * rb_bytes_per_sample(r->in_buffer);
	int byte_count_out=float_count_out * rb_bytes_per_sample(r->in_buffer);

	//partial or no data left, resampler swingout not filled yet
	if(r->in_buffer->no_more_input_data 
		&& rb_can_read(r->in_buffer) >=0
		&& rb_can_read(r->in_buffer) < byte_count_in
		&& !rb_resampler_swingout->no_more_input_data
	)
	{
		int frames_left=rb_can_read(r->in_buffer)/rb_channel_count(r->in_buffer)/rb_bytes_per_sample(r->in_buffer);
//		fprintf(stderr,"\nrs_process(): partial data in r->in_buffer (frames): %d\n",frames_left);

		//adding zero pad to get full output of resampler
		int final_frames=frames_left + rs_get_pad_size_end(r);

		float *interleaved_frame_buffer=new float [ final_frames  * rb_channel_count(r->in_buffer) ];

		//read from r->in_buffer
		rb_read(r->in_buffer
			,(char*)interleaved_frame_buffer
			,frames_left * rb_channel_count(r->in_buffer) * rb_bytes_per_sample(r->in_buffer));

//		fprintf(stderr,"rb_read r->in_buffer after final frames read: %zu\n",rb_can_read(r->in_buffer));

		//write to resampler swingout buffer (to process in accurate chunks)
		size_t wrote=rb_write(rb_resampler_swingout
			,(const char*)interleaved_frame_buffer
			,final_frames  * rb_channel_count(r->in_buffer) * rb_bytes_per_sample(r->in_buffer)
		);

//		fprintf(stderr,"\nwrote %zu bytes to resampler swingout buffer (frames: %d, containing %d pad frames)\n"
//			,wrote,final_frames,rs_get_pad_size_end(r));

		///
		rb_resampler_swingout->no_more_input_data=1;

		delete[] interleaved_frame_buffer;
	}

	if( ( rb_can_read(r->in_buffer) > 0
		|| rb_can_read(rb_resampler_swingout) >0 )///
		&& rb_can_write(r->out_buffer) >= byte_count_out
	)
	{
/*
		fprintf(stderr,"\nrs_process(): normal operation in %zu swingout %zu write %zu \n"
			,rb_can_read(r->in_buffer)
			,rb_can_read(rb_resampler_swingout)
			,rb_can_write(r->out_buffer)
		);
*/

		float *interleaved_frame_buffer=new float [float_count_in];
		float *buffer_resampling_out=new float [float_count_out];

		//condition to jump into while loop
		R.out_count=1;

		int byte_count_out_eff=0;

		int resampler_loop_counter=0;

		rb_t *buffer_to_use;
		if(rb_can_read(rb_resampler_swingout) >0 )

		{
//			fprintf(stderr,"\n*** using swingout buffer\n");
			buffer_to_use=rb_resampler_swingout;
		}
		else
		{
//			fprintf(stderr,"\nxxx using normal buffer\n");
			buffer_to_use=r->in_buffer;
		}

		//while resampler needs more input for one output period
		while(R.out_count>0)
		{
			//read from r->in_buffer, just peek / don't move read pointer yet
			size_t peeked=rb_peek(buffer_to_use
				,(char*)interleaved_frame_buffer
				,byte_count_in);
			
			int R_in_count;
			int R_out_count;

			if(peeked!=byte_count_in)
			{
//				fprintf(stderr,"\nin rs_process(): could not peek %d bytes, just %zu!\n",byte_count_in,peeked);
				R_in_count=rb_byte_to_frame_count(buffer_to_use,peeked);
				R_out_count=R_in_count*r->out_to_in_sr_ratio;
			}
			else
			{
				R_in_count=r->input_period_frames;
				R_out_count=r->output_period_frames;
			}
/*
			if(R_in_count<1)
			{///
			}
			if(R_out_count<1)
			{///}
*/

			//if out count 0 -> input will never be processed. at least one frame
			R_out_count=MAX(1,R_out_count);

			//configure for next resampler process cycle
			R.inp_data=interleaved_frame_buffer;
			R.inp_count=R_in_count;
			R.out_data=buffer_resampling_out;
			R.out_count=R_out_count;


//			fprintf(stderr,"\n--- rs_process(): before inpcount %d outcount %d\n",R.inp_count,R.out_count);
			R.process();
//			fprintf(stderr,"\n--- rs_process(): after inpcount %d outcount %d loop %d\n",R.inp_count,R.out_count,resampler_loop_counter);

			///
			byte_count_out_eff+=R_out_count * rb_channel_count(buffer_to_use) * rb_bytes_per_sample(buffer_to_use);

/*
			if(R.inp_count>0)
			{
				fprintf(stderr,"--- rs_process(): /!\\ after process r.inp_count %d\n",R.inp_count);
				//this probably means that the remaining input sample is not yet processed to out
				//we'll use it again for the next cycle (feed as the first sample of the next processing block)
			}
*/
			//- remaining inp_count!
			int bytes_processed=rb_frame_to_byte_count(buffer_to_use,R_in_count-R.inp_count);

			//advance as many input bytes as resampler could process
			size_t advanced=rb_advance_read_index(
				buffer_to_use
				,bytes_processed);

			if(advanced!=bytes_processed)
			{
				fprintf(stderr,"\nin rs_process(): could not advance %d bytes!\n",bytes_processed);
			}

			r->total_input_frames_resampled+=(R_in_count-R.inp_count);

			resampler_loop_counter++;
/*
			if(resampler_loop_counter>1)
			{
				fprintf(stderr,"\n/!\\ resample loop counter > 1\n");
			}
*/

		}//end while(R.out_count>0)


		//finally write resampler output to r->out_buffer
		size_t wrote=rb_write(r->out_buffer
			,(const char*)buffer_resampling_out
			,byte_count_out_eff);
///			,byte_count_out);

		delete[] interleaved_frame_buffer;
		delete[] buffer_resampling_out;

		if(wrote!=byte_count_out)
		{
//			fprintf(stderr,"\nin rs_process(): did not write %d bytes, wrote %zu!\n",byte_count_out,wrote);
			///
			r->resampling_finished=1;
		}

		return rb_byte_to_frame_count(buffer_to_use,wrote);
	}
	///else

	return 0;
}//end rs_process()

#endif
//EOF
