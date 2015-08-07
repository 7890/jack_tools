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

#ifndef JACKAUDIO_H_INC
#define JACKAUDIO_H_INC

#include "buffers.h"
#include "jack_playfile.h"

typedef jack_default_audio_sample_t sample_t;

static const char **ports;
static jack_status_t status;

//array of pointers to JACK input or output ports
static jack_port_t **ioPortArray;
static jack_client_t *client;
//http://jack-audio.10948.n7.nabble.com/jack-options-t-td3483.html
static jack_options_t jack_opts = jack_options_t(JackNoStartServer | JackServerName);

static void jack_init();
static void jack_post_init();
static int jack_process(jack_nframes_t nframes, void *arg);
static void jack_open_client();
static void jack_register_output_ports();
static void jack_activate_client();
static void jack_connect_output_ports();
static void jack_fill_output_buffers_zero();
static void jack_register_callbacks();
static void jack_shutdown_handler (void *arg);

//=============================================================================
static void jack_init()
{
	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?).\nthis is fatal: jack_playfile needs JACK to run.\n");
		fprintf(stderr,"see http://jackaudio.org for more information on the JACK Audio Connection Kit.\n");
		exit(1);
	}

	if(server_name==NULL || strlen(server_name)<1)
	{
		server_name="default";
	}

	if(client_name==NULL)
	{
		client_name="jack_playfile";
	}

	//create an array of output ports
	//calloc() zero-initializes the buffer, while malloc() leaves the memory uninitialized
	ioPortArray = (jack_port_t**) calloc(
		output_port_count * sizeof(jack_port_t*), sizeof(jack_port_t*));

}//end init_jack()

//=============================================================================
static void jack_post_init()
{
	if(client!=NULL)
	{
		jack_server_down=0;

		jack_period_frames=jack_get_buffer_size(client);
		jack_sample_rate=jack_get_sample_rate(client);

		jack_cycles_per_second=(float)jack_sample_rate / jack_period_frames;

		jack_output_data_rate_bytes_per_second=jack_sample_rate * output_port_count * bytes_per_sample;
		out_to_in_byte_ratio=jack_output_data_rate_bytes_per_second/file_data_rate_bytes_per_second;

		fprintf(stderr,"JACK sample rate: %d\n",jack_sample_rate);
		fprintf(stderr,"JACK period size: %d frames\n",jack_period_frames);
		fprintf(stderr,"JACK cycles per second: %.2f\n",jack_cycles_per_second);
		fprintf(stderr,"JACK output data rate: %.1f bytes/s (%.2f MB/s)\n",jack_output_data_rate_bytes_per_second
			,(jack_output_data_rate_bytes_per_second/1000000));
		fprintf(stderr,"total byte out_to_in ratio: %f\n", out_to_in_byte_ratio);
	}
}//end jack_post_init()

//=============================================================================
static int jack_process(jack_nframes_t nframes, void *arg) 
{
	if(shutdown_in_progress || !process_enabled)
	{
//		fprintf(stderr,"process(): process not enabled or shutdown in progress\n");
		return 0;
	}

	if(nframes!=jack_period_frames)
	{
		fprintf(stderr,"/!\\ process(): JACK period size has changed during playback.\njack_playfile can't handle that :(\n");
		shutdown_in_progress=1;
		return 0;
	}

	if(reset_ringbuffers_in_progress)
	{
//		fprintf(stderr,"?");
		jack_fill_output_buffers_zero();
		return 0;
	}

	if(is_idling_at_end)
	{
//		fprintf(stderr,"!");
		jack_fill_output_buffers_zero();
		req_buffer_from_disk_thread();
		return 0;
	}

	if(seek_frames_in_progress)
	{
		//test if already enough data available to play
		if(jack_ringbuffer_read_space(rb_deinterleaved) 
			>= jack_period_frames * output_port_count * bytes_per_sample)
		{
			seek_frames_in_progress=0;
		}
		else
		{
			req_buffer_from_disk_thread();
		}
	}

	resample();
	deinterleave();

	if(!is_playing || (seek_frames_in_progress && !loop_enabled))// && !all_frames_read))
	{
//		fprintf(stderr,".");
		jack_fill_output_buffers_zero();
		return 0;
	}

	if(all_frames_read
		&& jack_ringbuffer_read_space(rb_interleaved)==0
		&& jack_ringbuffer_read_space(rb_resampled_interleaved)==0
		&& jack_ringbuffer_read_space(rb_deinterleaved)==0)
	{
//		fprintf(stderr,"process(): all frames read and no more data in rb_interleaved, rb_resampled_interleaved, rb_deinterleaved\n");
//		fprintf(stderr,"process(): shutdown condition 1 met\n");

		jack_fill_output_buffers_zero();

		shutdown_in_progress=1;
		return 0;
	}

	//count at start of enabled, non-zero (seek) cycles (1st cycle = #1)
	process_cycle_count++;

	//normal operation
	if(jack_ringbuffer_read_space(rb_deinterleaved) 
		>= jack_period_frames * output_port_count * bytes_per_sample)
	{
//		fprintf(stderr,"process(): normal output to JACK buffers in cycle %" PRId64 "\n",process_cycle_count);
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],jack_period_frames);
			//put samples from ringbuffer to JACK output buffer
			jack_ringbuffer_read(rb_deinterleaved
				,(char*)o1
				,jack_period_frames * bytes_per_sample);

			if(add_markers)
			{
				o1[0]=marker_first_sample_normal_jack_period;
			}
		}
		total_frames_pushed_to_jack+=jack_period_frames;

		print_stats();
	}

	//partial data left
	else if(all_frames_read && jack_ringbuffer_read_space(rb_deinterleaved)>0)
	{
		int remaining_frames=jack_ringbuffer_read_space(rb_deinterleaved)/output_port_count/bytes_per_sample;
//		fprintf(stderr,"process(): partial data, remaining frames in db_deinterleaved:  %d\n", remaining_frames);

		//use what's available
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],jack_period_frames);

			//put samples from ringbuffer to JACK output buffer
			jack_ringbuffer_read(rb_deinterleaved
				,(char*)o1
				,remaining_frames * bytes_per_sample);

			if(add_markers)
			{
				o1[0]=marker_first_sample_last_jack_period;
			}

			//pad the rest to have a full JACK period
			for(int i=0;i<jack_period_frames-remaining_frames;i++)
			{
				if(add_markers)
				{
					o1[remaining_frames+i]=marker_pad_samples_last_jack_period;
				}
				else
				{
					o1[remaining_frames+i]=0;
				}
			}

			if(add_markers)
			{
				o1[jack_period_frames-1]=marker_last_sample_last_jack_period;
			}
		}

		//don't count pad frames
		total_frames_pushed_to_jack+=remaining_frames;

//		fprintf(stderr,"process(): rb_deinterleaved can read after last samples (expected 0) %d\n"
//			,jack_ringbuffer_read_space(rb_deinterleaved));

		//other logic will detect shutdown condition met to clear buffers
		return 0;
	}//end partial data
	else
	{
		//this should not happen
		process_cycle_underruns++;
		fprintf(stderr,"\nprocess(): /!\\ ======== underrun\n");

		jack_fill_output_buffers_zero();
		print_stats();
	}

	req_buffer_from_disk_thread();

	return 0;
}//end process()

//=============================================================================
static void jack_open_client()
{
	client = jack_client_open (client_name, jack_opts, &status, server_name);
}

//=============================================================================
static void jack_register_output_ports()
{
	//register each output port
	for (int port=0 ; port<output_port_count ; port ++)
	{
		//create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr, "/!\\ could not create portname for port %d\n", port);
			free_ringbuffers();
			exit(1);
		}

		//register the output port
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port] == NULL) 
		{
			fprintf(stderr, "/!\\ could not create output port %d\n", (port+1));
			free_ringbuffers();
			exit(1);
		}
	}
}

//=============================================================================
static void jack_activate_client()
{


	//now activate client in JACK, starting with process() cycles
	if (jack_activate (client)) 
	{
		fprintf (stderr, "/!\\ cannot activate client\n\n");
		free_ringbuffers();
		exit(1);
	}
}

//=============================================================================
static void jack_connect_output_ports()
{
	/*
	const char** jack_get_ports 	( jack_client_t *,
			const char *  	port_name_pattern,
			const char *  	type_name_pattern,
			unsigned long  	flags 
	) 	
	*/
	//prevent to get physical midi ports
	const char* pat="audio";

	ports = jack_get_ports (client, NULL, pat,
				JackPortIsPhysical|JackPortIsTerminal|JackPortIsInput);
	if (ports == NULL) 
	{
		fprintf(stderr, "/!\\ no physical playback ports found\n");
		free_ringbuffers();
		exit(1);
	}

	//test (stereo)
	if(connect_to_sisco)
	{
//		const char *left_out= "Simple Scope (Stereo) GTK:in1";
//		const char *right_out="Simple Scope (Stereo) GTK:in2";
		const char *left_out= "Simple Scope (3 channel) GTK:in1";
		const char *right_out="Simple Scope (3 channel) GTK:in2";

		jack_connect (client, jack_port_name(ioPortArray[0]) , left_out);
		jack_connect (client, jack_port_name(ioPortArray[1]) , right_out);
		//override
		//autoconnect_jack_ports=0;
	}

	if(autoconnect_jack_ports)
	{
		int k=0;
		int i=0;
		for(i;i<output_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& ioPortArray[k]!=NULL 
				&& jack_port_name(ioPortArray[k])!=NULL)
			{
				if((int)(ports[i][0])<32)
				{
//					fprintf(stderr,"(what's wrong here? doesn't happen with jack2) %d\n",ports[i][0]);
					break;
				}

				if(!jack_connect (client, jack_port_name(ioPortArray[k]) , ports[i]))
				{
					//used variabled can't be NULL here
					fprintf (stderr, "autoconnect: %s -> %s\n",
						jack_port_name(ioPortArray[k]),ports[i]);
					k++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						jack_port_name(ioPortArray[k]),ports[i]);
				}
			}
		}
	}

	free (ports);
}//end connect_jack_ports()

//=============================================================================
static void jack_fill_output_buffers_zero()
{
	//fill buffers with silence (last cycle before shutdown (?))
	for(int i=0; i<output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from JACK for that channel
		o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],jack_period_frames);
		//set all samples zero
		memset(o1, 0, jack_period_frames*bytes_per_sample);
	}
}

//=============================================================================
static void jack_register_callbacks()
{
	jack_set_process_callback (client, jack_process, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);
}

//=============================================================================
static void jack_shutdown_handler (void *arg)
{
//	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr, "\n/!\\ JACK server down!\n");

	jack_server_down=1;
}

#endif
//EOF
