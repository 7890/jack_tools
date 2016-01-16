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

#ifndef JACKAUDIO_H_INC
#define JACKAUDIO_H_INC

#include "config.h"
#include "resampler.h"
#include "jack_playfile.h"

typedef jack_default_audio_sample_t sample_t;

static void init_jack_struct();
static void init_debug_marker_struct();

static int jack_init();
static void jack_error(const char* err);
static int jack_wait_connect();
static void jack_post_init();
static int jack_process(jack_nframes_t nframes, void *arg);
static int jack_open_client();
static int jack_register_output_ports();
static int jack_activate_client();
static void jack_connect_output_ports();
static void jack_fill_output_buffers_zero();
static void jack_register_callbacks();
static int jack_xrun_handler(void *);
static void jack_close_down();
static void jack_shutdown_handler(void *arg);

///hack, resampler
rs_t *r1;

//=============================================================================
static void init_jack_struct()
{
	jack=new JackServer;

	jack->server_name = NULL; //default: 'default'
	jack->client_name = NULL; //default: 'jack_playfile'

	jack->sample_rate=0;
	jack->period_frames=0;
	jack->period_duration=0;
	jack->cycles_per_second=0;
	jack->output_data_rate_bytes_per_second=0;

	jack->process_enabled=0;
	jack->server_down=1;

	jack->process_cycle_count=0;
	jack->process_cycle_underruns=0;
	jack->total_frames_pushed_to_jack=0;

	jack->options = jack_options_t(JackNoStartServer | JackServerName);
	//jack->status=;
	//jack->transport_state=;
	//jack->ioPortArray=; //set in jack_register_output_ports()
	jack->client=NULL;

	jack->try_reconnect=1;
	jack->autoconnect_ports=1;
//	jack->use_transport=0;

	jack->output_port_count=0; //not yet known

	jack->volume_coefficient=1.0;
	jack->volume_amplification_decibel=0.0;
	jack->clipping_detected=0;
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
static int jack_init()
{
	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?).\nthis is fatal: jack_playfile needs JACK to run.\n");
		fprintf(stderr,"see http://jackaudio.org for more information on the JACK Audio Connection Kit.\n");
		return 0;
	}

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
		jack->output_port_count * sizeof(jack_port_t*), sizeof(jack_port_t*));

	jack_set_error_function(jack_error);

	return 1;
}//end jack_init()

//===================================================================
static void jack_error(const char* err)
{
	///suppress for now
}

//=============================================================================
static int jack_wait_connect()
{
	fprintf (stderr, "\r%s\rwaiting for connection to JACK server...",clear_to_eol_seq);

	while(jack->client==NULL)
	{
		if (!jack_open_client())
		{
//			fprintf (stderr, "/!\\ jack_client_open() failed, status = 0x%2.0x\n", jack->status);
			if(!jack->try_reconnect)
			{
				fprintf (stderr, " failed.\n");
				return 0;
			}
#ifdef WIN32
			Sleep(1000);
#else
			usleep(1000000);
#endif
		}
	}//end while client==NULL

	fprintf (stderr, "\r%s\r",clear_to_eol_seq);

	return 1;
}//end jack_wait_connect()

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
		jack->period_duration=(double)1000*jack->period_frames / jack->sample_rate;
		jack->cycles_per_second=(double)jack->sample_rate / jack->period_frames;

		jack->output_data_rate_bytes_per_second=jack->sample_rate * jack->output_port_count * bytes_per_sample;
//		running->out_to_in_byte_ratio=jack->output_data_rate_bytes_per_second/file_data_rate_bytes_per_second;

		if(settings->is_verbose)
		{
			fprintf(stderr,"JACK sample rate: %d\n",jack->sample_rate);
			fprintf(stderr,"JACK period size: %d frames\n",jack->period_frames);
			fprintf(stderr,"JACK period duration: %.3f ms\n",jack->period_duration);
			fprintf(stderr,"JACK cycles per second: %.3f\n",jack->cycles_per_second);
//			fprintf(stderr,"JACK output data rate: %.1f bytes/s (%.2f MB/s)\n",jack->output_data_rate_bytes_per_second
//				,(jack->output_data_rate_bytes_per_second/1000000));
		}
		jack->server_down=0;
	}
}//end jack_post_init()

//=============================================================================
static int jack_process(jack_nframes_t nframes, void *arg) 
{
	if(running->shutdown_in_progress || !jack->process_enabled)
	{
//		fprintf(stderr,"jack_process(): process not enabled or shutdown in progress\n");
		return 0;
	}

	if(nframes!=jack->period_frames)
	{
		fprintf(stderr,"/!\\ jack_process(): JACK period size has changed during playback.\njack_playfile can't handle that :(\n");
		running->shutdown_in_progress=1;
		return 0;
	}

	if(transport->use_jack_transport)
	{
		jack->transport_state = jack_transport_query(jack->client, NULL);

		if(jack->transport_state == JackTransportStarting || jack->transport_state == JackTransportRolling)
		//if(jack->transport_state == JackTransportRolling)
		{
			transport->is_playing=1;
		} 
		else if (jack->transport_state == JackTransportStopped)
		{
			transport->is_playing=0;
		}
	}

	if(reset_ringbuffers_in_progress)
	{
		jack_fill_output_buffers_zero();
		return 0;
	}

	if(transport->is_idling_at_end)
	{
		jack_fill_output_buffers_zero();
		req_buffer_from_disk_thread();
		return 0;
	}

	///lazy if paused
	if(transport->is_playing || transport->loop_enabled)
	{
		rs_process(r1);
		deinterleave();
	}

	if(
		(rb_interleaved->no_more_input_data || rb_resampled_interleaved->no_more_input_data)
		&& !rb_can_read(rb_interleaved)
		&& !rb_can_read(rb_resampled_interleaved)
		&& !rb_can_read(rb_deinterleaved)
	)
	{
//		fprintf(stderr,"jack_process(): all frames read and no more data in rb_interleaved, rb_resampled_interleaved, rb_deinterleaved\n");
//		fprintf(stderr,"jack_process(): shutdown condition 1 met\n");
		jack_fill_output_buffers_zero();
		running->shutdown_in_progress=1;
		return 0;
	}

	if(!transport->is_playing || (running->seek_frames_in_progress && !transport->loop_enabled))
	{
//		fprintf(stderr,".");
		jack_fill_output_buffers_zero();
		req_buffer_from_disk_thread();
		return 0;
	}

	//count at start of enabled, non-zero (seek) cycles (1st cycle = #1)
	jack->process_cycle_count++;

	//normal operation
	if(rb_can_read_frames(rb_deinterleaved)>=jack->period_frames)
	{
//		fprintf(stderr,"jack_process(): normal output to JACK buffers in cycle %" PRId64 "\n",jack->process_cycle_count);
		for(int i=0; i<jack->output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

			if(i<running->channel_count)
			{
				//put samples from ringbuffer to JACK output buffer
				rb_read(rb_deinterleaved
					,(char*)o1
					,jack->period_frames * bytes_per_sample);
			}
			else
			{
				//fill remaining channels to match requested channel_count
				//set all samples zero
				memset(o1, 0, jack->period_frames*bytes_per_sample);
			}

			if(settings->add_markers)
			{
				o1[0]=debug_marker->first_sample_normal_jack_period;
			}
		}
		jack->total_frames_pushed_to_jack+=jack->period_frames;
		print_stats();
	}

	//partial or no left
	else if( (rb_interleaved->no_more_input_data || rb_resampled_interleaved->no_more_input_data)
		&& rb_can_read(rb_deinterleaved)>=0)
	{
		//multichannel
		int remaining_frames=rb_can_read_frames(rb_deinterleaved);

//		fprintf(stderr,"\njack_process(): partial data, remaining frames in db_deinterleaved:  %d\n", remaining_frames);
		///
		if(remaining_frames<1)
		{
			jack_fill_output_buffers_zero();
			running->shutdown_in_progress=1;
			return 0;
		}

		//use what's available
		for(int i=0; i<jack->output_port_count; i++)
		{
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

			if(i<running->channel_count)
			{
				//put samples from ringbuffer to JACK output buffer
				rb_read(rb_deinterleaved
					,(char*)o1
					,remaining_frames * bytes_per_sample);
			}
			else
			{
				//fill remaining channels to match requested channel_count
				//set all samples zero
				memset(o1, 0, jack->period_frames*bytes_per_sample);
			}

			if(settings->add_markers)
			{
				o1[0]=debug_marker->first_sample_last_jack_period;
			}

			//pad the rest to have a full JACK period
			for(int i=0;i<jack->period_frames-remaining_frames;i++)
			{
				if(settings->add_markers)
				{
					o1[remaining_frames+i]=debug_marker->pad_samples_last_jack_period;
				}
				else
				{
					o1[remaining_frames+i]=0;
				}
			}

			if(settings->add_markers)
			{
				o1[jack->period_frames-1]=debug_marker->last_sample_last_jack_period;
			}
		}

		//don't count pad frames
		jack->total_frames_pushed_to_jack+=remaining_frames;

//		fprintf(stderr,"jack_process(): rb_deinterleaved can read after last samples (expected 0) %d\n"
//			,rb_can_read(rb_deinterleaved));

		//other logic will detect shutdown condition met to clear buffers
		return 0;
	}//end partial data
	else
	{
		//this should not happen
		jack->process_cycle_underruns++;
//		fprintf(stderr,"\njack_process(): /!\\ ======== underrun\n");
		jack_fill_output_buffers_zero();
		print_stats();
	}

	req_buffer_from_disk_thread();

	return 0;
}//end process()

//=============================================================================
static int jack_open_client()
{
	jack->client = jack_client_open (jack->client_name, jack->options, &jack->status, jack->server_name);
	if(jack->client==NULL)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

//=============================================================================
static int jack_register_output_ports()
{
	//register each output port
	for (int port=0 ; port<jack->output_port_count ; port ++)
	{
		//create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr, "/!\\ could not create portname for port %d\n", port);
			return 0;
		}

		//register the output port
		jack->ioPortArray[port] = jack_port_register(jack->client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (jack->ioPortArray[port] == NULL) 
		{
			fprintf(stderr, "/!\\ could not create output port %d\n", (port+1));
			return 0;
		}
	}
	return 1;
}

//=============================================================================
static int jack_activate_client()
{
	//now activate client in JACK, starting with process() cycles
	if (jack_activate (jack->client)) 
	{
		fprintf (stderr, "/!\\ cannot activate client\n\n");
		free_ringbuffers();
		return 0;
	}
	return 1;
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
	const char **ports;

	ports = jack_get_ports (jack->client, NULL, pat,
				JackPortIsPhysical|JackPortIsTerminal|JackPortIsInput);

	if (ports == NULL) 
	{
		fprintf(stderr, "/!\\ no physical playback ports found\n");
		return;
	}

	//test (stereo)
	if(settings->connect_to_sisco)
	{
		///needs limit if less io ports
		jack_connect (jack->client, jack_port_name(jack->ioPortArray[0]) , cisco_in1);
		jack_connect (jack->client, jack_port_name(jack->ioPortArray[1]) , cisco_in2);
	}

	if(jack->autoconnect_ports)
	{
		int k=0;
		int i=0;
		for(i;i<jack->output_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& jack->ioPortArray[k]!=NULL 
				&& jack_port_name(jack->ioPortArray[k])!=NULL)
			{
				if((int)(ports[i][0])<32) //not a printable char, not good
				{
					break;
				}

				if(!jack_connect (jack->client, jack_port_name(jack->ioPortArray[k]) , ports[i]))
				{
					if(settings->is_verbose)
					{
						//used variabled can't be NULL here
						fprintf (stderr, "autoconnect: %s -> %s\n",
							jack_port_name(jack->ioPortArray[k]),ports[i]);
					}
					k++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						jack_port_name(jack->ioPortArray[k]),ports[i]);
				}
			}
		}
	}//end if(jack->autoconnect_ports)
	free(ports);
}//end jack_connect_output_ports()

//=============================================================================
static void jack_fill_output_buffers_zero()
{
	//fill buffers with silence (last cycle before shutdown (?))
	for(int i=0; i<jack->output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from JACK for that channel
		o1=(sample_t*)jack_port_get_buffer(jack->ioPortArray[i],jack->period_frames);

//		float last_sample=o1[jack->period_frames-1];

		//set all samples zero
		memset(o1, 0, jack->period_frames*bytes_per_sample);
	}

}//end jack_fill_output_buffers_zero()

//=============================================================================
static void jack_register_callbacks()
{
	jack_set_process_callback (jack->client, jack_process, NULL);
	jack_on_shutdown(jack->client, jack_shutdown_handler, 0);
	jack_set_xrun_callback(jack->client, jack_xrun_handler, NULL);
}

//=============================================================================
static int jack_xrun_handler(void *)
{
	fprintf(stderr,"!!!!XRUN\n");
}

//=============================================================================
static void jack_close_down()
{
	if(jack->client!=NULL)
	{
		jack_deactivate(jack->client);
//		fprintf(stderr,"JACK client deactivated.\n");
		int index=0;
		while(jack->ioPortArray[index]!=NULL && index<jack->output_port_count)
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
	fprintf(stderr, "\n/!\\ JACK server down!\n");
	//remember playout position if jack comes back to display clock correctly
	running->last_seek_pos=get_current_play_position_in_file();
	jack->server_down=1;
}

#endif
//EOF
