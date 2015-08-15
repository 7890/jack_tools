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

static void init_jack_struct();
static void init_debug_marker_struct();

static void jack_init();
static void jack_post_init();
static int jack_process(jack_nframes_t nframes, void *arg);
static void jack_open_client();
static void jack_register_output_ports();
static void jack_activate_client();
static void jack_connect_output_ports();
static void jack_fill_output_buffers_zero();
static void jack_register_callbacks();
static void jack_close_down();
static void jack_shutdown_handler (void *arg);

static const char **ports;

//when cycle must be zeroed out (i.e. while seeking), "pseudo"-fade-out last value
//fade-in non-zeroed out cycles following zeroed-out cycles
static int fade_length=16;

//will be reset on cycles where sample data is available
static int last_cycle_was_zeroed_out=0;

//=============================================================================
static void init_jack_struct()
{
	jack=new JackServer;

	jack->server_name = NULL; //default: 'default'
	jack->client_name = NULL; //default: 'jack_playfile'

	jack->process_enabled=0;

	jack->sample_rate=0;
	jack->period_frames=0;
	jack->cycles_per_second=0;
	jack->output_data_rate_bytes_per_second=0;
	jack->server_down=1;
	jack->process_cycle_count=0;
	jack->process_cycle_underruns=0;
	jack->total_frames_pushed_to_jack=0;
	jack->options = jack_options_t(JackNoStartServer | JackServerName);
	//jack->status=;
	//jack->transport_state=;
	//jack->ioPortArray=;
	jack->client=NULL;

	jack->try_reconnect=1;
	jack->autoconnect_ports=1;
	jack->use_transport=0;
}//end init_jack_struct()

//=============================================================================
static void init_debug_marker_struct()
{
	debug_marker=new DebugMarker;

	debug_marker->first_sample_normal_jack_period=0.2;
	debug_marker->last_sample_out_of_resampler=-0.5;
	debug_marker->first_sample_last_jack_period=0.9;
	debug_marker->pad_samples_last_jack_period=-0.2;
	debug_marker->last_sample_last_jack_period=-0.9;
}

//=============================================================================
static void jack_init()
{
	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?).\nthis is fatal: jack_playfile needs JACK to run.\n");
		fprintf(stderr,"see http://jackaudio.org for more information on the JACK Audio Connection Kit.\n");
		exit(1);
	}

//	init_jack_struct(); in main() -> don't overwrite settings here
	init_debug_marker_struct();

	if(jack->server_name==NULL || strlen(jack->server_name)<1)
	{
		jack->server_name="default";
	}

	if(jack->client_name==NULL)
	{
		jack->client_name="jack_playfile";
	}

	//create an array of output ports
	//calloc() zero-initializes the buffer, while malloc() leaves the memory uninitialized
	jack->ioPortArray = (jack_port_t**) calloc(
		output_port_count * sizeof(jack_port_t*), sizeof(jack_port_t*));

}//end init_jack()

//=============================================================================
static void wait_connect_jack()
{
	fprintf (stderr, "\r%s\rwaiting for connection to JACK server...",clear_to_eol_seq);

	//http://stackoverflow.com/questions/4832603/how-could-i-temporary-redirect-stdout-to-a-file-in-a-c-program
	int bak_stderr, bak_stdout, new_;

	while(jack->client==NULL)
	{
		//hide possible error output from jack temporarily
		fflush(stderr);
		bak_stderr = dup(fileno(stderr));

#ifndef WIN32
		new_ = open("/dev/null", O_WRONLY);

		dup2(new_, fileno(stderr));
		close(new_);

#else
		new_ = open("nul", O_WRONLY);
		dup2(new_, fileno(stderr));
		close(new_);

		fflush(stdout);
		bak_stdout = dup(fileno(stdout));

		new_ = open("nul", O_WRONLY);
		dup2(new_, fileno(stdout));
		close(new_);
#endif

		//open a client connection to the JACK server
		jack_open_client();

		//show stderr again
		fflush(stderr);
		dup2(bak_stderr, fileno(stderr));
		close(bak_stderr);

#ifdef WIN32
		//show stdout again
		fflush(stdout);
		dup2(bak_stdout, fileno(stdout));
		close(bak_stdout);
#endif

		if (jack->client == NULL) 
		{
//			fprintf (stderr, "/!\\ jack_client_open() failed, status = 0x%2.0x\n", jack->status);

			if(!jack->try_reconnect)
			{
				fprintf (stderr, " failed.\n");
				exit(1);
			}
#ifdef WIN32
			Sleep(1000);
#else
			usleep(1000000);
#endif
		}
	}//end while client==NULL

	fprintf (stderr, "\r%s\r",clear_to_eol_seq);

}//end wait_connect_jack()


//=============================================================================
static void jack_post_init()
{
	if(jack->client!=NULL)
	{
		//more stable reconnect to JACK
		while(jack_get_sample_rate(jack->client)<1)
		{
			usleep(100);
		}

		jack->period_frames=jack_get_buffer_size(jack->client);
		jack->sample_rate=jack_get_sample_rate(jack->client);

		jack->cycles_per_second=(float)jack->sample_rate / jack->period_frames;

		jack->output_data_rate_bytes_per_second=jack->sample_rate * output_port_count * bytes_per_sample;
		out_to_in_byte_ratio=jack->output_data_rate_bytes_per_second/file_data_rate_bytes_per_second;

		fprintf(stderr,"JACK sample rate: %d\n",jack->sample_rate);
		fprintf(stderr,"JACK period size: %d frames\n",jack->period_frames);
		fprintf(stderr,"JACK cycles per second: %.2f\n",jack->cycles_per_second);
		fprintf(stderr,"JACK output data rate: %.1f bytes/s (%.2f MB/s)\n",jack->output_data_rate_bytes_per_second
			,(jack->output_data_rate_bytes_per_second/1000000));
		fprintf(stderr,"total byte out_to_in ratio: %f\n", out_to_in_byte_ratio);

		jack->server_down=0;
	}
}//end jack_post_init()

//=============================================================================
static int jack_process(jack_nframes_t nframes, void *arg) 
{
	if(shutdown_in_progress || !jack->process_enabled)
	{
//		fprintf(stderr,"process(): process not enabled or shutdown in progress\n");
		return 0;
	}

	if(nframes!=jack->period_frames)
	{
		fprintf(stderr,"/!\\ process(): JACK period size has changed during playback.\njack_playfile can't handle that :(\n");
		shutdown_in_progress=1;
		return 0;
	}

/*
  int  jack_transport_reposition (jack_client_t *client, jack_position_t *pos);
  int  jack_transport_locate (jack_client_t *client, jack_nframes_t frame);
*/
	if(jack->use_transport)
	{
		jack->transport_state = jack_transport_query(jack->client, NULL);

		if (jack->transport_state == JackTransportStarting || jack->transport_state == JackTransportRolling)
		{
			is_playing=1;
		} 
		else if (jack->transport_state == JackTransportStopped)
		{
			is_playing=0;
		}
	}

	if(reset_ringbuffers_in_progress)
	{
//		fprintf(stderr,"\n?\n");
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
//		if(all_frames_read)
//		{
//			seek_frames_in_progress=0;
//		}
		//test if already enough data available to play
		if(jack_ringbuffer_read_space(rb_deinterleaved) 
			>= jack->period_frames * channel_count_use_from_file * bytes_per_sample)
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

	if(!is_playing || (seek_frames_in_progress && !loop_enabled))
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
	jack->process_cycle_count++;

	//normal operation
	if(jack_ringbuffer_read_space(rb_deinterleaved) 
		>= jack->period_frames * channel_count_use_from_file * bytes_per_sample)
	{
//		fprintf(stderr,"process(): normal output to JACK buffers in cycle %" PRId64 "\n",jack->process_cycle_count);
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

			if(i<channel_count_use_from_file)
			{
				//put samples from ringbuffer to JACK output buffer
				jack_ringbuffer_read(rb_deinterleaved
					,(char*)o1
					,jack->period_frames * bytes_per_sample);
			}
			else //fill remaining channels to match requested channel_count
			{
				//set all samples zero
		                memset(o1, 0, jack->period_frames*bytes_per_sample);
			}

			if(last_cycle_was_zeroed_out)
			{
				for(int k=0; k<fade_length; k++)
				{
//					fprintf(stderr,"\n%.5f\n",o1[k]);
					o1[k]*=((float)1)/(fade_length-k);
//					fprintf(stderr,"\n%.5f\n",o1[k]);
				}
			}

			if(add_markers)
			{
				o1[0]=debug_marker->first_sample_normal_jack_period;
			}
		}
		jack->total_frames_pushed_to_jack+=jack->period_frames;

		last_cycle_was_zeroed_out=0;

		print_stats();
	}

	//partial data left
	else if(all_frames_read && jack_ringbuffer_read_space(rb_deinterleaved)>0)
	{
		int remaining_frames=jack_ringbuffer_read_space(rb_deinterleaved)/channel_count_use_from_file/bytes_per_sample;
//		fprintf(stderr,"process(): partial data, remaining frames in db_deinterleaved:  %d\n", remaining_frames);

		//use what's available
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

			if(i<channel_count_use_from_file)
			{
				//put samples from ringbuffer to JACK output buffer
				jack_ringbuffer_read(rb_deinterleaved
					,(char*)o1
					,remaining_frames * bytes_per_sample);
			}
			else //fill remaining channels to match requested channel_count
			{
				//set all samples zero
		                memset(o1, 0, jack->period_frames*bytes_per_sample);
			}

			if(last_cycle_was_zeroed_out)
			{
				for(int k=0; k<fade_length; k++)
				{
//					fprintf(stderr,"\n%.5f\n",o1[k]);
					o1[k]*=((float)1)/(fade_length-k);
//					fprintf(stderr,"\n%.5f\n",o1[k]);
				}
			}

			if(add_markers)
			{
				o1[0]=debug_marker->first_sample_last_jack_period;
			}

			//pad the rest to have a full JACK period
			for(int i=0;i<jack->period_frames-remaining_frames;i++)
			{
				if(add_markers)
				{
					o1[remaining_frames+i]=debug_marker->pad_samples_last_jack_period;
				}
				else
				{
					o1[remaining_frames+i]=0;
				}
			}

			if(add_markers)
			{
				o1[jack->period_frames-1]=debug_marker->last_sample_last_jack_period;
			}
		}

		//don't count pad frames
		jack->total_frames_pushed_to_jack+=remaining_frames;

		last_cycle_was_zeroed_out=0;

//		fprintf(stderr,"process(): rb_deinterleaved can read after last samples (expected 0) %d\n"
//			,jack_ringbuffer_read_space(rb_deinterleaved));

		//other logic will detect shutdown condition met to clear buffers
		return 0;
	}//end partial data
	else
	{
		//this should not happen
		jack->process_cycle_underruns++;
//		fprintf(stderr,"\nprocess(): /!\\ ======== underrun\n");

		jack_fill_output_buffers_zero();
		print_stats();
	}

	req_buffer_from_disk_thread();

	return 0;
}//end process()

//=============================================================================
static void jack_open_client()
{
	jack->client = jack_client_open (jack->client_name, jack->options, &jack->status, jack->server_name);
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
		jack->ioPortArray[port] = jack_port_register(jack->client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (jack->ioPortArray[port] == NULL) 
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
	if (jack_activate (jack->client)) 
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

	ports = jack_get_ports (jack->client, NULL, pat,
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

		jack_connect (jack->client, jack_port_name(jack->ioPortArray[0]) , left_out);
		jack_connect (jack->client, jack_port_name(jack->ioPortArray[1]) , right_out);
	}

	if(jack->autoconnect_ports)
	{
		int k=0;
		int i=0;
		for(i;i<output_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& jack->ioPortArray[k]!=NULL 
				&& jack_port_name(jack->ioPortArray[k])!=NULL)
			{
				if((int)(ports[i][0])<32)
				{
//					fprintf(stderr,"(what's wrong here? doesn't happen with jack2) %d\n",ports[i][0]);
					break;
				}

				if(!jack_connect (jack->client, jack_port_name(jack->ioPortArray[k]) , ports[i]))
				{
					//used variabled can't be NULL here
					fprintf (stderr, "autoconnect: %s -> %s\n",
						jack_port_name(jack->ioPortArray[k]),ports[i]);
					k++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						jack_port_name(jack->ioPortArray[k]),ports[i]);
				}
			}
		}
	}

	free(ports);
}//end connect_jack_ports()

//=============================================================================
static void jack_fill_output_buffers_zero()
{
	/*
	when getting a buffer from JACK, it will be left untouched since it was last filled in client process().
	if the previous buffer was non-empty, the transition from the last sample to zero can be too harsh.

	idea how to pseudo-fade to zero:
	using the last sample of the previous buffer, then fading that linearly to zero.
	this is not a problem if the previous buffer already was a silent one, fade out will still result in 0.

	     .
	   _/|\
	     | \
	_____|__\__

	1/fade_length:

	1/0 -> hm.
	1/1 -> 1
	1/2 -> 0.5
	1/3 -> 0.333..
	etc
	*/


	//fill buffers with silence (last cycle before shutdown (?))
	for(int i=0; i<output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from JACK for that channel
		o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

		float last_sample=o1[jack->period_frames-1];

		//set all samples zero
		memset(o1, 0, jack->period_frames*bytes_per_sample);

		for(int k=0; k<fade_length; k++)
		{
//			fprintf(stderr,"\n%.5f\n",last_sample);
			o1[k]=last_sample*((float)1/(k+1));
//			fprintf(stderr,"\n%.5f\n",o1[k]);
		}
	}

	last_cycle_was_zeroed_out=1;

}//end jack_fill_output_buffers_zero()

//=============================================================================
static void jack_register_callbacks()
{
	jack_set_process_callback (jack->client, jack_process, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(jack->client, jack_shutdown_handler, 0);
}

//=============================================================================
static void jack_close_down()
{
	if(jack->client!=NULL)
	{
		jack_deactivate(jack->client);
//		fprintf(stderr,"JACK client deactivated. ");

		int index=0;
		while(jack->ioPortArray[index]!=NULL && index<output_port_count)
		{
			jack_port_unregister(jack->client,jack->ioPortArray[index]);
			index++;
		}

//		fprintf(stderr,"JACK ports unregistered\n");

		jack_client_close(jack->client);
//		fprintf(stderr,"JACK client closed\n");
	}
}//end jack_close_down()

//=============================================================================
static void jack_shutdown_handler (void *arg)
{
//	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr, "\n/!\\ JACK server down!\n");

	jack->server_down=1;
}

#endif
//EOF
