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

//simple file player for JACK
//inspired by jack_play, libsndfile, zresampler

#include <signal.h>
#include <getopt.h>

#include "common.h"

//disk thread
static pthread_t disk_thread={0};
static pthread_mutex_t disk_thread_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ok_to_read=PTHREAD_COND_INITIALIZER;
static int disk_thread_initialized=0;
static int disk_thread_finished=0;

//disk reads
//first cycle indicated as 1
static uint64_t disk_read_cycle_count=0;

//how many frames to seek
//calculated / limited in seek_frames and seek_frames_absolute
//executed in disk_thread
static uint64_t frames_to_seek=0;
static uint64_t frames_to_seek_type=SEEK_CUR; //or SEEK_SET

//use to signal next file, resulting in partial init (not a full JACK client shutdown/register)
static int prepare_for_next_file=0;

//how many bytes one sample (of one channel) is using in the file
static int bytes_per_sample_in_file=0;

//disk reads read into this buffer
static float *frames_from_file_buffer;

//how many frames to read per request
static int sndfile_request_frames=0;

//reported by stat()
static uint64_t file_size_bytes=0;

//derive from sndfile infos or estimate based on filesize & framecount
static float file_data_rate_bytes_per_second=0;
static uint64_t total_bytes_read_from_file=0;
static uint64_t total_frames_read_from_file=0;

//=============================================================================
int main(int argc, char *argv[])
{
	//init structs so that options can be put there
	init_settings();
	init_transport();
	init_jack_struct();

	kb_init_term_seq();

	parse_cmdline_args(argc,argv);

	//this will copy from settings to "running" properties for next file
	init_running_properties();

	//handle ctrl+c
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	if(settings->keyboard_control_enabled)
	{
		kb_init_key_codes();
		//now set raw to read key hits
		kb_set_terminal_raw();
	}

	if(!pl_create(argc,argv,settings->read_from_playlist,settings->dump_usable_files))
	{
		//clean up / reset and quit
		signal_handler(44);
	}
	//all done
	if(settings->dump_usable_files)
	{
		signal_handler(42); //quit nicely
	}

	while(!pl_open_check_file() || !open_init_file(files_to_play[current_playlist_index].c_str()))
	{
		pl_set_next_index(PL_DIRECTION_FORWARD);
		if(no_more_files_to_play)
		{
			fprintf(stderr,"/!\\ no valid files in playlist\n");
			//clean up / reset and quit
			signal_handler(44); //quit with error
		}
		fprintf(stderr,"\n\n");
	}

	if(settings->keyboard_control_enabled)
	{
		//turn off cursor
		fprintf(stderr,"%s",turn_off_cursor_seq);
	}

	if(!jack_init())
	{
		//clean up / reset and quit
		signal_handler(44);
	}

//=======
//outer loop, start over if JACK went down and came back
//option !--noreconnect
while(true)
{
	//if for some reason (i.e. last file arg was invalid) and shutdown_in_progress requested,
	//jump over init stuff and let main loop handle nice shutdown
	if(running->shutdown_in_progress)
	{
		goto _main_loop;
	}

	if(!jack_wait_connect())
	{
		//clean up / reset and quit
		signal_handler(44);
	}
	jack_post_init();

	running->out_to_in_byte_ratio=jack->output_data_rate_bytes_per_second/file_data_rate_bytes_per_second;

	if(settings->is_verbose)
	{
		fprintf(stderr,"JACK output data rate: %.1f bytes/s (%.2f MB/s)\n",jack->output_data_rate_bytes_per_second
			,(jack->output_data_rate_bytes_per_second/1000000));
	}

	//sampling rate ratio output (JACK) to input (file)
	if(settings->custom_file_sample_rate>0)
	{
		if(settings->is_verbose)
		{
			fprintf(stderr,"using custom file sample rate: %d\n", settings->custom_file_sample_rate);
		}

		float speed=(double)settings->custom_file_sample_rate/sf_info_generic.sample_rate;
		fprintf(stderr,"speed: %.3f     %s\n"
			,speed
			,sin_format_duration_str( sin_get_seconds(&sf_info_generic) / speed )
			);
		//from now on, the file (info) will look like it would be originally using the custom sample rate
		sf_info_generic.sample_rate=settings->custom_file_sample_rate;
	}

	fprintf(stderr,        "play duration:   %s\n"
		,sin_format_duration_str( (double)running->frame_count/sf_info_generic.sample_rate)
	);

	//-----------------
	setup_ringbuffers(running->channel_count,sf_info_generic.sample_rate,jack->sample_rate);


	rs_free(r1);

	//setup resampler (filtsize=0 for default)
	r1=rs_new(settings->resampler_filtersize,rb_interleaved,rb_resampled_interleaved,jack->period_frames);
	//-----------------

	if(r1==NULL)
	{
		fprintf(stderr,"hint: custom file sample rates where\n\trate %%modulo 50 = 0\nshould work in most cases.");
		running->shutdown_in_progress=1;
		goto _main_loop;
	}


	if(settings->is_verbose)
	{
		fprintf(stderr,"resampler filter size: %d\n",r1->filtersize);
		fprintf(stderr,"resampler out_to_in ratio: %.3f\n",r1->out_to_in_sr_ratio);
		fprintf(stderr,        "file frames: %zu -> JACK frames: %d (excl. padding start %d end %d)\n"
			,running->frame_count
			,(int)(running->frame_count * (double)sf_info_generic.sample_rate / jack->sample_rate)
			,rs_get_pad_size_start(r1)
			,rs_get_pad_size_end(r1)
		);
	}

	if(r1->out_to_in_sr_ratio!=1)
	{
		//how many frames to read per disk_read
		sndfile_request_frames=r1->input_period_frames;
		//add some to be faster than realtime
		int additional=MAX(2,sndfile_request_frames*0.5 *(float)1.0/r1->out_to_in_sr_ratio );
		sndfile_request_frames+=additional;
	}
	else
	{
		sndfile_request_frames=jack->period_frames;
		//add some to be faster than realtime
		int additional=MAX(2,sndfile_request_frames*0.1);
		sndfile_request_frames+=additional;
	}

	fprintf(stderr,"sndfile_request_frames: %d\n",sndfile_request_frames);

	delete[] frames_from_file_buffer;
	frames_from_file_buffer=new float[sndfile_request_frames*running->channel_count];

	if(settings->is_verbose)
	{
		fprintf(stderr,"total byte out_to_in ratio: %f (%.3f * %.3f)\n"
			,running->out_to_in_byte_ratio*r1->out_to_in_sr_ratio
			,running->out_to_in_byte_ratio
			,r1->out_to_in_sr_ratio
		);
	}
	//set seek step size here to consider possibly overridden file sample rate
	//(10 seconds should seek 10 seconds in overridden sr domain)
	set_frames_from_exponent();
	set_seconds_from_exponent();

	setup_disk_thread();

	//request initial frames from file
	{
	int sleep_count=6;
	int sleep_=(int)(float)200000/jack->cycles_per_second;

//	fprintf(stderr,"sleep_ %d * %d us   %.3f ms\n",sleep_count,sleep_,(double)sleep_count*sleep_/1000);
	for(int i=0;i<sleep_count;i++)
	{
//		fprintf(stderr,"*");
		req_buffer_from_disk_thread();
		usleep(sleep_);
	}
//	fprintf(stderr,"\n");
	}

	//only do this part of initialization if not already done
	if(!prepare_for_next_file)
	{
		jack_register_callbacks();

		if(!jack_register_output_ports() || !jack_activate_client())
		{
			//clean up / reset and quit
			signal_handler(44);
		}

		if(jack->autoconnect_ports)
		{
			jack_connect_output_ports();
		}
	}
	prepare_for_next_file=0;

	jack->process_enabled=1;
//	fprintf(stderr,"jack->process_enabled\n");

_main_loop:
	//run until interrupted
	while (1) 
	{
		if(jack->server_down)
		{
			if(jack->try_reconnect)
			{
				goto _start_all_over;
			}
			else
			{
				running->shutdown_in_progress=1;
			}
		}

		//try clean shutdown, mainly to avoid possible audible glitches 
		if(running->shutdown_in_progress && !running->shutdown_in_progress_signalled)
		{
			running->shutdown_in_progress_signalled=1;

			if(no_more_files_to_play)
			{
				//effectively shutdown
				signal_handler(42);
			}
			else
			{
				//signal about to load and play next file in args
				//using existing JACK client and settings (i.e. no port re-connection)
				prepare_for_next_file=1;

				jack->process_enabled=0;
				reset_ringbuffers();

				goto _start_all_over;
			}
		}//end if(running->shutdown_in_progress && !running->shutdown_in_progress_signalled)
		else if(settings->keyboard_control_enabled)
		{
			fprintf(stderr,"\r");

			//flicker
			//go to start of line, add spaces ~"clear", go to start of line
			//fprintf(stderr,"\r%s\r",clear_to_eol_seq);

			if(running->seek_frames_in_progress)
			{
				fprintf(stderr,"...seeking  ");
			}
			else
			{
				if(transport->is_playing)
				{
					fprintf(stderr,">  playing  ");
				}
				else
				{
					fprintf(stderr,"|| paused   ");
				}
			}

			if(transport->use_jack_transport)
			{
				fprintf(stderr,"J");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(transport->is_muted)
			{
				fprintf(stderr,"M");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(jack->clipping_detected)
			{
				fprintf(stderr,"!");
				jack->clipping_detected=0;
			}
			else if(jack->volume_amplification_decibel!=0)
			{
				fprintf(stderr,"A");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(transport->loop_enabled)
			{
				fprintf(stderr,"L");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(transport->pause_at_end)
			{
				fprintf(stderr,"P  ");
			}
			else
			{
				fprintf(stderr,"   ");
			}

			kb_print_clock();

			kb_handle_key_hits();
		}//end if settings->keyboard_control_enabled
#ifdef WIN32
		Sleep(10);
#else
		usleep(10000);
#endif
	}//end while true (inner, main / key handling loop

_start_all_over:

	//the main loop was breaked by jumping here
	//this can have several reasons:
	//-JACK went away -> wait and try to connect again
	//-done playing file -> check if there is another file in args to play
	//in both cases break out to outer loop

	jack->process_enabled=0;
	running->shutdown_in_progress=0;
	running->shutdown_in_progress_signalled=0;

	if(jack->server_down)
	{
		//will be created again once JACK available
		jack->client=NULL;
		//leave intact as much as possible to retake playing at pos where JACK went away
	}
	else //if(!jack->server_down)
	{
		sin_close();
		fprintf(stderr,"\n\n");

		//use current value
		pl_set_next_index(playlist_advance_direction);
		//reset to default
		playlist_advance_direction=PL_DIRECTION_FORWARD;

		if(pl_open_check_file())
		{
			if(!open_init_file(files_to_play[current_playlist_index].c_str()))
			{
				running->shutdown_in_progress=1;
			}
		}
		else
		{
			running->shutdown_in_progress=1;
		}
	}

//=======
}//end while true (outer, JACK down/reconnect)
	exit(0);
}//end main

//=============================================================================
static int open_init_file(const char *f)
{
//	fprintf(stderr,"open_init_file %s\n",f);

	filename=f;
	if(!(sin_open(filename,0)))
	{
		sin_close();
		return 0;
	}

	//"kill" thread
	pthread_mutex_unlock(&disk_thread_lock);
	pthread_cond_signal(&ok_to_read);
	pthread_join(disk_thread, NULL);
	pthread_cancel(disk_thread);

	//reset some variables
	disk_thread_initialized=0;
	set_all_frames_read(0);
	total_frames_read_from_file=0;
	transport->is_idling_at_end=0;

	running->frame_offset=settings->frame_offset;
	running->frame_count=settings->frame_count;

	running->channel_offset=settings->channel_offset;
	running->channel_count=settings->channel_count;

	struct stat st;
	stat(filename, &st);
	file_size_bytes = st.st_size;

	fprintf(stderr,"file #%4d/%4d: %s\n"
		,1+current_playlist_index
		,(int)files_to_play.size()
		,filename);

	if(settings->is_verbose)
	{
		fprintf(stderr,"size:        %"PRId64" bytes (%.2f MB)\n",file_size_bytes,(float)file_size_bytes/1000000);
	}

	//print file format info
	bytes_per_sample_in_file=sin_file_info(sf_info_generic,settings->is_verbose);

	if(!settings->is_verbose)
	{
		fprintf(stderr,"total duration:  %s\n", sin_generate_duration_str(&sf_info_generic));
	}

//----------handle / limit frame offset, count

	//offset can't be greater total frames in file
	if(running->frame_offset>=sf_info_generic.frames)
	{
		fprintf(stderr,"/!\\ frame_offset greater or equal as frames in file.\n");
		return 0;
	}

	//if requested count negative, zero or greater than frames in file
	//limit count
	if(running->frame_count<=0 || running->frame_count>sf_info_generic.frames)
	{
		//set possible max respecting frame_offset
		running->frame_count=sf_info_generic.frames-running->frame_offset;

		if(settings->is_verbose)
		{
			fprintf(stderr,"frame_count set to: %"PRId64"",running->frame_count);
			if(running->frame_count==sf_info_generic.frames)
			{
				fprintf(stderr," (all available frames)");
			}
			fprintf(stderr,"\n");
		}
	}

	//offset + count can't be greater than frames in file
	//limit count
	if( (running->frame_offset+running->frame_count) > sf_info_generic.frames)
	{
		//set possible max respecting frame_offset
		running->frame_count=MIN((sf_info_generic.frames-running->frame_offset),running->frame_count);

		if(settings->is_verbose)
		{
			fprintf(stderr,"frame_count set to: %"PRId64"\n",running->frame_count);
		}
	}

	//if for some reason from==to (count==0)
	if(running->frame_count==0)
	{
		fprintf(stderr,"/!\\ no frames in selection, nothing to play\n");
		return 0;
	}

//----------handle / limit channel offset, count
/*
	testcase: 8 channel file

	(no options)		offset     0, count auto  8, output auto 8   file channels 1,2,3,4,5,6,7,8
	-O1			offset set 1, count limit 7, output auto 7   file channels   2,3,4,5,6,7,8
	-C1			offset     0, count set   1, output auto 1   file channels 1

	-O1 -C1			offset set 1, count set   1, output auto 1   file channels   2


	-w1			offset     0, count limit 1, output set  1   file channels 1


	-O1 -C4 -w6		offset set 1, count set   4, output set  6   file channels   2,3,4,5

	-O6 -C4 -w6		offset set 6, count limit 2, output set  6   file channels             7,8

	-O6 -C4 -w1		ffset set  6, count limit 1, output set  1   file channels             7


	-O8			offset >= file channel count:		not possible
	-C9			(offset +) count >= file channel count:	limit
*/

	if(jack->output_port_count>0)
	{
		if(settings->is_verbose)
		{
			fprintf(stderr,"output port count (fixed): %d\n",jack->output_port_count);
		}
	}

	//offset can't be greater total channels in file
	if(running->channel_offset>=sf_info_generic.channels)
	{
		fprintf(stderr,"/!\\ channel_offset greater or equal as channels in file.\n");
		return 0;
	}

	if(running->channel_count==0) //automatically use all channels from offset to end
	{
		running->channel_count=sf_info_generic.channels - running->channel_offset;
		fprintf(stderr,"channel count automatically set to %d\n",running->channel_count);
	}
	//else explicit file channel count requested

	//limit channel count
	if(running->channel_count<=0)
	{
		fprintf(stderr,"/!\\ no channels in selection (i.e. channel offset beyond or equal file channel count), nothing to play\n");
		return 0;
	}

	if(running->channel_offset+running->channel_count > sf_info_generic.channels)
	{
		running->channel_count=sf_info_generic.channels-running->channel_offset;
		fprintf(stderr,"/!\\ limiting channel_count to %d (file)\n",running->channel_count);
	}

	//if explicit output port count given, limit to it
	if(jack->output_port_count>0)
	{
		if(running->channel_count>jack->output_port_count)
		{
			fprintf(stderr,"/!\\ limiting channel_count to %d (JACK)\n",jack->output_port_count);
		}
		running->channel_count=MIN(running->channel_count,jack->output_port_count);
	}
	else
	{
		//sets the channel_count for all following files
		jack->output_port_count=running->channel_count;
		if(settings->is_verbose)
		{
			fprintf(stderr,"output port count: %d\n",jack->output_port_count);
		}

	}
	//running->channel_count sets the number of channels in the ringbuffers
	//it's a derived value that meets file properties, processing options and JACK output port count

	if(settings->is_verbose)
	{
		fprintf(stderr,"playing frames (offset count end): %"PRId64" %"PRId64" %"PRId64"\n"
			,running->frame_offset
			,running->frame_count
			,MIN(sf_info_generic.frames,running->frame_offset+running->frame_count));
	}

	if(settings->is_verbose)
	{
		fprintf(stderr,"playing channels (offset count last): %d %d %d\n"
			,running->channel_offset
			,running->channel_count
			,running->channel_offset+running->channel_count);

		fprintf(stderr,"amplification: %.1f dB (%.3f)\n"
			,jack->volume_amplification_decibel
			,jack->volume_coefficient);
	}

	if(bytes_per_sample_in_file<=0 || is_opus || is_mpg123 || is_ogg || is_flac)
	{
		//try estimation: total filesize (including headers, other chunks ...) divided by (frames*channels*native bytes)
		file_data_rate_bytes_per_second=(float)file_size_bytes
			/sin_get_seconds(&sf_info_generic);

		if(settings->is_verbose)
		{
			fprintf(stderr,"disk read file (1:1): %.1f bytes/s (%.2f MB/s) average, estimated\n"
				,file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
		}
	}
	else
	{
		file_data_rate_bytes_per_second=sf_info_generic.sample_rate * sf_info_generic.channels * bytes_per_sample_in_file;

		if(settings->is_verbose)
		{
			fprintf(stderr,"disk read file (1:1): %d bytes/s (%.2f MB/s)\n",(int)file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
		}
	}

	if( settings->is_verbose && (file_data_rate_bytes_per_second/1000000) > 20 )
	{
		fprintf(stderr,"/!\\ this is a relatively high data rate\n");
	}

	//initial seek
	if(running->frame_offset>0)
	{
		frames_to_seek=running->frame_offset;
		frames_to_seek_type=SEEK_SET;
		running->seek_frames_in_progress=1;
	}
	else
	{
		frames_to_seek=0;
		running->seek_frames_in_progress=0;
	}
//	fprintf(stderr,"seek frames %"PRId64"\n",seek_frames_per_hit);

	return 1;
}//end open_init_file()

//=============================================================================
static int disk_read_frames()
{
//	fprintf(stderr,"disk_read_frames() called\n");
	uint64_t frames_to_go=running->frame_count-total_frames_read_from_file;

//	fprintf(stderr,"disk_read_frames(): frames to go %" PRId64 "\n",frames_to_go);

	//only read/write as many frames as requested (frame_count)
	int frames_read=(int)MIN(frames_to_go,sndfile_request_frames);

	if(frames_read<=0 && !transport->is_idling_at_end)
	{
		set_all_frames_read(1);
		return 0;
	}

	sf_count_t frames_read_from_file=0;

	rb_t *rb_to_use;

	if(!transport->is_idling_at_end)
	{

		if(r1->out_to_in_sr_ratio==1.0)
		{
			//directly write to rb_resampled_interleaved (skipping rb_interleaved)
			rb_to_use=rb_resampled_interleaved;
		}
		else 
		{
			//write to rb_interleaved
			rb_to_use=rb_interleaved;
		}

		if(rb_can_write_frames(rb_to_use) < sndfile_request_frames )
		{
//			fprintf(stderr,"/!\\ disk_read_frames() not enough space in ringbuffer for one cycle\n");
			return 0;
		}

		//frames_read: number of (multi-channel) samples to read, i.e. 1 frame in a stereo file = two values

		//get float frames from any of the readers, requested size ensured to be returned except eof
		frames_read_from_file=sin_read_frames_from_file_to_buffer(frames_from_file_buffer
			,frames_read,running->channel_offset,running->channel_count);

		//put to the selected ringbuffer
		rb_write(rb_to_use,(const char*)frames_from_file_buffer
			,rb_frame_to_byte_count(rb_to_use,frames_read_from_file));
	}//end if(!transport->is_idling_at_end)

	if(frames_read_from_file>0)
	{
		disk_read_cycle_count++;
		total_bytes_read_from_file+=frames_read_from_file * sf_info_generic.channels * bytes_per_sample_in_file;

		total_frames_read_from_file+=frames_read_from_file;
//		fprintf(stderr,"disk_read_frames(): frames: read %"PRId64" total %"PRId64"\n",frames_read_from_file,total_frames_read_from_file);

		if(total_frames_read_from_file>=running->frame_count)
		{
			set_all_frames_read(1);
			if(transport->pause_at_end)
			{
#ifndef WIN32
				fprintf(stderr,"pae ");
#endif
				total_frames_read_from_file=running->frame_count;
				transport->is_playing=0;
				running->seek_frames_in_progress=0;
				frames_to_seek=0;
				transport->is_idling_at_end=1;
			}

			if(transport->loop_enabled)
			{
#ifndef WIN32
				if(settings->keyboard_control_enabled)
				{
					///
					fprintf(stderr,"loop ");
				}
#endif
				total_frames_read_from_file=0;
				set_all_frames_read(0);

				running->seek_frames_in_progress=1;
				///
				running->last_seek_pos=sin_seek(running->frame_offset,SEEK_SET);
				running->seek_frames_in_progress=0;

				//current play position / clock depends on last_seek_pos and rb_deinterleaved total_bytes_read
				rb_reset_stats(rb_deinterleaved);

				transport->is_idling_at_end=0;
				r1->resampling_finished=0;
				return 1;
			}
		}//end if(total_frames_read_from_file>=frame_count)
		return frames_read_from_file;
	}//end if(frames_read_from_file>0)
	//if no frames were read assume we're at EOF
	set_all_frames_read(1);
	return 0;
}//end disk_read_frames()

//this method is called from disk_thread (pthread_t)
//it will be called only once and then loop/wait until a condition to finish is met
//=============================================================================
static void *disk_thread_func(void *arg)
{
	//assume soundfile not null

	//seek to given offset position
	running->last_seek_pos=sin_seek(running->frame_offset,SEEK_SET);

	//===main disk loop
	for(;;)
	{
//		fprintf(stderr,"disk_thread_func() loop\n");

		//check if seek is due
		if(running->seek_frames_in_progress)
		{
			///
			reset_ringbuffers();
			rs_flush(r1);

			running->last_seek_pos=sin_seek(frames_to_seek,frames_to_seek_type);

			running->seek_frames_in_progress=0;
			frames_to_seek=0;

			total_frames_read_from_file=running->last_seek_pos-running->frame_offset;

			//reset some variables before update
			set_all_frames_read(0);
			transport->is_idling_at_end=0;
			r1->resampling_finished=0;
			if(total_frames_read_from_file>=running->frame_count)
			{
				set_all_frames_read(1);
				if(transport->pause_at_end)
				{
#ifndef WIN32
					fprintf(stderr,"pae ");
#endif
					transport->is_idling_at_end=1;
					transport->is_playing=0;
				}

				if(transport->loop_enabled)
				{
#ifndef WIN32
					///totally wrong display
					if(settings->keyboard_control_enabled)
					{
						fprintf(stderr,"loop ");
					}
#endif
					total_frames_read_from_file=0;
					set_all_frames_read(0);
					running->seek_frames_in_progress=1;
					///
					running->last_seek_pos=sin_seek(running->frame_offset,SEEK_SET);
					running->seek_frames_in_progress=0;
					transport->is_idling_at_end=0;

					r1->resampling_finished=0;
					continue;
				}
			}//end total_frames_read_from_file>=running->frame_count

//			fprintf(stderr,"\nseek end === new pos %"PRId64" total read %"PRId64"\n",running->last_seek_pos,total_frames_read_from_file);
		}

		//no resampling needed
		if(r1->out_to_in_sr_ratio==1.0)
		{
			if(rb_can_write_frames(rb_resampled_interleaved)<sndfile_request_frames)
			{
				//===wait here until process() requests to continue
				pthread_cond_wait (&ok_to_read, &disk_thread_lock);
				//once waked up, restart loop
				continue;
			}
		}
		else //if out_to_in_sr_ratio!=1.0
		{
			if(rb_can_write_frames(rb_interleaved)<sndfile_request_frames)
			{
				//===wait here until process() requests to continue
				pthread_cond_wait (&ok_to_read, &disk_thread_lock);
				//once waked up, restart loop
				continue;
			}
		}

		//for both resampling, non-resampling
		//disk_read() returns 0 on EOF
		if(!disk_read_frames())
		{
			if(!transport->is_idling_at_end)
			{
				//not idling so eof
				goto done;
			}
			//idling so continuing
		}

		//===wait here until process() requests to continue
		pthread_cond_wait (&ok_to_read, &disk_thread_lock);
	}//end main loop
done:
//	sf_close_();//close in shutdown handler

	pthread_mutex_unlock (&disk_thread_lock);
	pthread_join(disk_thread, NULL);
	pthread_cancel(disk_thread);

//	fprintf(stderr,"\ndisk_thread_func(): disk thread finished\n");

	disk_thread_finished=1;
	return 0;
}//end disk_thread_func()

//=============================================================================
static void setup_disk_thread()
{
	disk_thread_finished=0;
	if(disk_thread_initialized)
	{
//		fprintf(stderr,"/!\\ already have disk_thread, using that one\n");
		return;
	}

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//initially lock
	pthread_mutex_lock(&disk_thread_lock);
	//create the disk_thread (pthread_t) with start routine disk_thread_func
	//disk_thread_func will be called after thread creation
	//(not attributes, no args)
	pthread_create(&disk_thread, NULL, disk_thread_func, NULL);
//	fprintf(stderr,"disk thread started\n");
	disk_thread_initialized=1;
}

//=============================================================================
static void req_buffer_from_disk_thread()
{
	if((all_frames_read() && !running->seek_frames_in_progress)
	|| reset_ringbuffers_in_progress)
	{
//		fprintf(stderr,"\nin req_buffer_from_disk_thread(): (all read, not seeking) or reset in progress\n");
		return;
	}

//	fprintf(stderr,"req_buffer_from_disk_thread()\n");

	if(rb_can_write_frames(rb_interleaved)<sndfile_request_frames)
	{
//		fprintf(stderr,"req_buffer_from_disk_thread(): /!\\ not enough write space in rb_interleaved\n");
		return;
	}

	//if possible to lock the disk_thread_lock mutex
	if(!pthread_mutex_trylock(&disk_thread_lock))
	{
//		fprintf(stderr,"new data from disk thread requested\n");
		//signal to disk_thread it can start or continue to read
		pthread_cond_signal (&ok_to_read);
		//unlock again
		pthread_mutex_unlock (&disk_thread_lock);
	}

}//end req_buffer_from_disk_thread()

//>=0
//=============================================================================
static void seek_frames_absolute(int64_t frames_abs)
{
	/*
	///
	if(running->seek_frames_in_progress)
	{
		return;
	}
	*/

	//limit absolute seek to given boundaries (frame_offset, frame_count)
	uint64_t seek_max=MAX(running->frame_offset,frames_abs);

	//seek in disk_thread
	frames_to_seek=MIN((running->frame_offset+running->frame_count),seek_max);
	frames_to_seek_type=SEEK_SET;

	running->seek_frames_in_progress=1;
}

//+ / -
//=============================================================================
static void seek_frames(int64_t frames_rel)
{
	if(running->seek_frames_in_progress)
	{
		return;
	}

	if(frames_rel==0)
	{
		//nothing to do
		return;
	}
/*
                            current abs pos    
         abs start          v                                   abs end
         |------------------------------------------------------|
                     |--------------------------|
                     frame_offset               offset + frame_count

                     |------|-------------------|

              .======x======.=============.=====x=======.
                     |      seek steps          |
                     limit                      limit
*/

	sf_count_t current_read_pos=get_current_play_position_in_file();

	int64_t seek=0;

	//limit relative seek to given boundaries (frame_offset, frame_count)
	if(frames_rel>0)
	{
		seek=MIN(
			(int64_t)(running->frame_offset + running->frame_count - current_read_pos)
			,frames_rel
		);
	}
	else //frames_rel<0
	{
		seek=MAX(

			(int64_t)(running->frame_offset - current_read_pos)
			,frames_rel
		);
	}

	//seek in disk_thread, always seek absolute
	seek_frames_absolute(current_read_pos+seek);
}

//=============================================================================
static void deinterleave()
{
//	fprintf(stderr,"deinterleave called\n");
	if(all_frames_read() && !rb_can_read(rb_resampled_interleaved))
	{
		//nothing to do
//		fprintf(stderr,"deinterleave(): disk thread finished and no more data in rb_resampled_interleaved\n");
		return;
	}

	int resampled_frames_avail=rb_can_read_frames(rb_resampled_interleaved);

	//if not limited, deinterleaved block align borked
	int resampled_frames_use=MIN(resampled_frames_avail,jack->period_frames);
//	fprintf(stderr,"deinterleave(): resampled frames avail: %d use: %d\n",resampled_frames_avail,resampled_frames_use);

	//deinterleave from resampled
	if((resampled_frames_use >= 1)
		&& (rb_can_write_frames(rb_deinterleaved) >=jack->period_frames)
	)
	{
//		fprintf(stderr,"deinterleave(): deinterleaving\n");

		void *data_resampled_interleaved;
		data_resampled_interleaved=malloc(resampled_frames_use * running->channel_count * bytes_per_sample);

		rb_read(rb_resampled_interleaved
			,(char*)data_resampled_interleaved
			,rb_frame_to_byte_count(rb_resampled_interleaved,resampled_frames_use));

		int bytepos_channel=0;

		for(int channel_loop=0; channel_loop < running->channel_count; channel_loop++)
		{
			bytepos_channel=channel_loop * bytes_per_sample;
			int bytepos_frame=0;

			for(int frame_loop=0; frame_loop < resampled_frames_use; frame_loop++)
			{
				bytepos_frame=bytepos_channel + frame_loop * running->channel_count * bytes_per_sample;
				//read 1 sample

				float f1=*( (float*)(data_resampled_interleaved + bytepos_frame) );

				if(jack->volume_coefficient!=1.0)
				{
						//apply amplification to change volume
						//===
						f1*=jack->volume_coefficient;
				}

				//show clipping even if muted
				if(f1>=1 && !jack->clipping_detected)
				{
					jack->clipping_detected=1;
				}

				if(transport->is_muted)
				{
					f1=0;
				}

				//put to ringbuffer
				rb_write(rb_deinterleaved
					,(char*)&f1
					,bytes_per_sample);
			}//frame
		}//channel

		free(data_resampled_interleaved);
//		fprintf(stderr,"===deinterleave(): done\n");
	}//end if enough data to deinterleave
}//end deinterleave()

//=============================================================================
static void set_all_frames_read(int all_read)
{
	if(rb_interleaved==NULL || rb_resampled_interleaved==NULL) {return;}
	rb_interleaved->no_more_input_data=all_read;
	rb_resampled_interleaved->no_more_input_data=all_read;
}

//=============================================================================
static int all_frames_read()
{
	if(rb_interleaved==NULL || rb_resampled_interleaved==NULL) {return 0;}
	return (rb_interleaved->no_more_input_data || rb_resampled_interleaved->no_more_input_data);
}

//=============================================================================
static uint64_t get_current_play_position_in_file()
{
	//the read position in the file is only equal to the play position after a seek (buffers empty).
	//if interleaved buffer is filled, the position must be calculated "back" from last seek position 
	//and deinterleave buffer fill level

	sf_count_t pos=running->last_seek_pos;
	sf_count_t pos_deinter=rb_byte_to_frame_count(rb_deinterleaved,rb_deinterleaved->total_bytes_read);
	pos+=pos_deinter*((float)sf_info_generic.sample_rate/jack->sample_rate);
	pos=MIN(running->frame_offset+running->frame_count,pos);
	return pos;
}

//=============================================================================
static void print_stats()
{
	if(!settings->debug)
	{
		return;
	}
	fprintf(stderr,"-stats: proc cycles %"PRId64" read cycles %"PRId64" proc underruns %"PRId64" bytes from file %"PRId64"\n-stats: frames: from file %"PRId64" input resampled %"PRId64" pushed to JACK %"PRId64"\n-stats: interleaved %lu resampled %lu deinterleaved %lu resampling finished %d all frames read %d disk thread finished %d\n"
		,jack->process_cycle_count
		,disk_read_cycle_count
		,jack->process_cycle_underruns
		,total_bytes_read_from_file

		,total_frames_read_from_file
		,r1->total_input_frames_resampled
		,jack->total_frames_pushed_to_jack

		,rb_can_read_frames(rb_interleaved)
		,rb_can_read_frames(rb_resampled_interleaved)
		,rb_can_read_frames(rb_deinterleaved)

		,r1->resampling_finished
		,all_frames_read()
		,disk_thread_finished
	);
}//end print_stats()

//=============================================================================
static void signal_handler(int sig)
{
	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
//	fprintf(stderr,"signal_handler() called\n");
	print_stats();

	if(jack->process_cycle_underruns>0)
	{
		fprintf(stderr,"/!\\ underruns: %"PRId64"\n",jack->process_cycle_underruns);
	}

//	fprintf(stderr,"expected frames pushed to JACK (excl. resampler padding): %f\n",(double)(frame_count * r1->out_to_in_sr_ratio) );

	if(sig!=42 && sig!=44 && settings->is_verbose)
	{
		fprintf(stderr, "terminate signal %d received\n",sig);
	}
	jack_close_down();
	sin_close();
//	fprintf(stderr,"soundfile closed\n");
	mpg123_exit();
	free_ringbuffers();
	kb_reset_terminal();

	///in sndfile.h
	delete[] tmp_buffer;

	///in playlist.h
	//"The simplest way to deallocate all the storage in a vector, without destroying the vector object itself, is"
	files_to_play = std::vector<string>();

	fprintf(stderr,"jack_playfile done.\n");
	fprintf(stderr,"%s",turn_on_cursor_seq);
	if(sig==44)
	{
		//exit with error
		exit(1);
	}
	else
	{
		//exit normally
		exit(0);
	}
}//end signal_handler()

//EOF
