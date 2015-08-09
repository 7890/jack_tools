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

static void open_init_file(const char *f);

//=============================================================================
int main(int argc, char *argv[])
{
	init_term_seq();

	int opt;
	//do until command line options parsed
	while(1)
	{
		//getopt_long stores the option index here
		int option_index=0;

		opt=getopt_long(argc, argv, "hvn:s:o:c:DRCEpmlfarkejL", long_options, &option_index);

		//Detect the end of the options
		if(opt==-1)
		{
			break;
		}
		switch(opt)
		{
			case 0:

			//If this option set a flag, do nothing else now
			if(long_options[option_index].flag!=0)
			{
				break;
			}

			case 'h':
				print_header();
				print_main_help();
				break;

			case 'v':
				print_version();
				exit(0);
				//break;

			case 'n':
				client_name=optarg;
				break;

			case 's':
				server_name=optarg;
				break;

			case 'o':
				frame_offset=strtoull(optarg, NULL, 10);
				break;

			case 'c':
				frame_count=strtoull(optarg, NULL, 10);
				break;

			case 'D':
				keyboard_control_enabled=0;
				break;

			case 'R':
				use_resampling=0;
				break;

			case 'C':
				autoconnect_jack_ports=0;
				break;

			case 'E':
				try_jack_reconnect=0;
				break;

			case 'p':
				is_playing=0;
				break;

			case 'm':
				is_muted=1;
				break;

			case 'l':
				loop_enabled=1;
				break;

			case 'f':
				is_time_seconds=0;
				break;

			case 'a':
				is_time_absolute=1;
				break;

			case 'r':
				is_time_elapsed=0;
				break;

			case 'k':
				is_clock_displayed=0;
				break;

			case 'e':
				pause_at_end=1;
				break;

			case 'j':
				use_jack_transport=1;
				break;

			case 'L': //libs
				fprintf(stderr,"here\n");
				exit(0);
				//break;

			case '?': //invalid commands
				//getopt_long already printed an error message
				print_header();
				fprintf(stderr, "Wrong arguments, see --help.\n\n");
				exit(1);
				break;

			default:
				break;
		 } //end switch op
	}//end while(1) parse args

	//remaining non optional parameters must be file
	if(argc-optind!=1)
	{
		print_header();
		fprintf(stderr, "Wrong arguments, see --help.\n\n");
		exit(1);
	}

	open_init_file(argv[optind]);

	jack_init();

//=======
//outer loop, start over if JACK went down and came back
//option try_jack_reconnect
while(true)
{
	fprintf (stderr, "\r%s\rwaiting for connection to JACK server...",clear_to_eol_seq);

	//http://stackoverflow.com/questions/4832603/how-could-i-temporary-redirect-stdout-to-a-file-in-a-c-program
	int bak_stderr, bak_stdout, new_;

	while(client==NULL)
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

		if (client == NULL) 
		{
//			fprintf (stderr, "/!\\ jack_client_open() failed, status = 0x%2.0x\n", status);

			if(!try_jack_reconnect)
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

	fflush(stderr);
	fprintf (stderr, "\r%s\r",clear_to_eol_seq);

	jack_post_init();

	//sampling rate ratio output (JACK) to input (file)
	out_to_in_sr_ratio=(double)jack_sample_rate/sf_info_generic.samplerate;

	if(use_resampling)
	{
		//ceil: request a bit more than needed to satisfy ratio
		//will result in inp_count>0 after process ("too much" input for out/in ratio, will always have output)
		sndfile_request_frames=ceil(jack_period_frames * (double)1/out_to_in_sr_ratio);
	}
	else
	{
		sndfile_request_frames=jack_period_frames;
	}

	setup_ringbuffers();

	setup_resampler();

	setup_disk_thread();

	jack_register_callbacks();
	jack_register_output_ports();

/*
	for(int i=0;i<jack_cycles_per_second;i++)
	{
		fprintf(stderr,"+");
*/
		//request first chunk from file
		req_buffer_from_disk_thread();
		usleep((double)100000/jack_cycles_per_second);
/*
	}

	fprintf(stderr,"\ninterleaved buffer %"PRId64" resampled frame buffer %"PRId64" bytes \n"
		,jack_ringbuffer_read_space(rb_interleaved)
		,jack_ringbuffer_read_space(rb_resampled_interleaved)
	);
*/

	jack_activate_client();
	jack_connect_output_ports();

	//handle ctrl+c
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	init_key_codes();

	//now set raw to read key hits
	set_terminal_raw();

	process_enabled=1;
//	fprintf(stderr,"process_enabled\n");

	//run until interrupted
	while (1) 
	{
		if(jack_server_down)
		{
			if(try_jack_reconnect)
			{
				goto _start_all_over;
			}
			else
			{
				shutdown_in_progress=1;
			}
		}

		//try clean shutdown, mainly to avoid possible audible glitches 
		if(shutdown_in_progress && !shutdown_in_progress_signalled)
		{
			shutdown_in_progress_signalled=1;
			signal_handler(42);
		}
		else if(keyboard_control_enabled)
		{
			fprintf(stderr,"\r");

			//flicker
			//go to start of line, add spaces ~"clear", go to start of line
			//fprintf(stderr,"\r%s\r",clear_to_eol_seq);

			if(seek_frames_in_progress)
			{
				fprintf(stderr,"...seeking  ");
			}
			else
			{
				if(is_playing)
				{
					fprintf(stderr,">  playing  ");
				}
				else
				{
					fprintf(stderr,"|| paused   ");
				}
			}

			if(use_jack_transport)
			{
				fprintf(stderr,"J");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(is_muted)
			{
				fprintf(stderr,"M");
			}
			else
			{
				fprintf(stderr," ");
			}

			if(loop_enabled)
			{
				fprintf(stderr,"L");
			}
			else
			{
				fprintf(stderr," ");
			}
			if(pause_at_end)
			{
				fprintf(stderr,"P  ");
			}
			else
			{
				fprintf(stderr,"   ");
			}

			print_clock();

			handle_key_hits();
		}//end if keyboard_control_enabled
#ifdef WIN32
		Sleep(10);
#else
		usleep(10000);
#endif
	}//end while true (inner, main / key handling loop

_start_all_over:
	process_enabled=0;
	shutdown_in_progress=0;
	shutdown_in_progress_signalled=0;

	//will be created again once JACK available
	client=NULL;
	//leave intact as much as possible to retake playing at pos where JACK went away
	reset_terminal();

//=======
}//end while true (outer, JACK down/reconnect)
	exit(0);
}//end main

//=============================================================================
static void open_init_file(const char *f)
{
	filename=f;

	memset (&sf_info_generic, 0, sizeof (sf_info_generic)) ;

	sin_open(filename,&sf_info_generic);

	struct stat st;
	stat(filename, &st);
	file_size_bytes = st.st_size;

	fprintf(stderr,"file:        %s\n",filename);
	fprintf(stderr,"size:        %"PRId64" bytes (%.2f MB)\n",file_size_bytes,(float)file_size_bytes/1000000);

	is_flac_=is_flac(&sf_info_generic);

	//flac and opus have different seek behaviour than wav or ogg (SEEK_END (+0) -> -1)
	if((is_opus || is_flac(&sf_info_generic)))
	{
		fprintf(stderr,"/!\\ reducing frame count by 1\n");
		sf_info_generic.frames=(sf_info_generic.frames-1);//not nice
	}

	if(sf_info_generic.frames<1)
	{
		fprintf(stderr,"/!\\ file has zero frames, nothing to play!\n");
		exit(1);
	}

	//for now: just create as many JACK client output ports as the file has channels
	output_port_count=sf_info_generic.channels;

	if(output_port_count<=0)
	{
		fprintf(stderr,"/!\\ file has zero channels, nothing to play!\n");
		exit(1);
	}

	bytes_per_sample_native=file_info(sf_info_generic,1);

	if(bytes_per_sample_native<=0 || is_opus || is_mpg123 || is_ogg_ || is_flac_)
	{
		//try estimation: total filesize (including headers, other chunks ...) divided by (frames*channels*native bytes)
		file_data_rate_bytes_per_second=(float)file_size_bytes
			/get_seconds(&sf_info_generic);

		fprintf(stderr,"disk read:   %.1f bytes/s (%.2f MB/s) average, estimated\n"
			,file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
	}
	else
	{
		file_data_rate_bytes_per_second=sf_info_generic.samplerate * output_port_count * bytes_per_sample_native;
		fprintf(stderr,"data rate:   %.1f bytes/s (%.2f MB/s)\n",file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
	}

	if( (file_data_rate_bytes_per_second/1000000) > 20 )
	{
		fprintf(stderr,"/!\\ this is a relatively high data rate\n");
	}

	//offset can't be negative or greater total frames in file
	if(frame_offset<0 || frame_offset>sf_info_generic.frames)
	{
		frame_offset=0;
		fprintf(stderr,"frame_offset set to %"PRId64"\n",frame_offset);
	}

	//if requested count negative, zero or greater total frames in file
	if(frame_count<=0 || frame_count>sf_info_generic.frames)
	{
		//set possible max respecting frame_offset
		frame_count=sf_info_generic.frames-frame_offset;
		fprintf(stderr,"frame_count set to %"PRId64"",frame_count);
		if(frame_count==sf_info_generic.frames)
		{
			fprintf(stderr," (all available frames)");
		}
		fprintf(stderr,"\n");
	}

	//offset + count can't be greater than frames in file
	if( (frame_offset+frame_count) > sf_info_generic.frames)
	{
		//set possible max respecting frame_offset
		frame_count=MIN((sf_info_generic.frames-frame_offset),frame_count);

		fprintf(stderr,"frame_count set to %"PRId64"\n",frame_count);
	}

	fprintf(stderr,"playing frames from/to/length: %"PRId64" %"PRId64" %"PRId64"\n"
		,frame_offset
		,MIN(sf_info_generic.frames,frame_offset+frame_count)
		,frame_count);

	//if for some reason from==to (count==0)
	if(frame_count==0)
	{
		fprintf(stderr,"/!\\ zero frames, nothing to do\n");
		exit(1);
	}

	//initial seek
	if(frame_offset>0)
	{
		seek_frames_in_progress=1;
	}

	//~1%
//	seek_frames_per_hit=ceil(frame_count / 100);

	set_frames_from_exponent();
	set_seconds_from_exponent();

//	fprintf(stderr,"seek frames %"PRId64"\n",seek_frames_per_hit);
}//end open_init_file()

//=============================================================================
static int disk_read_frames()
{
//	fprintf(stderr,"disk_read_frames() called\n");

	uint64_t frames_to_go=frame_count-total_frames_read_from_file;
//	fprintf(stderr,"disk_read_frames(): frames to go %" PRId64 "\n",frames_to_go);

	//only read/write as many frames as requested (frame_count)
	int frames_read=(int)MIN(frames_to_go,sndfile_request_frames);

	if(frames_read<=0 && !is_idling_at_end)
	{
		all_frames_read=1;
		return 0;
	}

	sf_count_t frames_read_from_file=0;

if(!is_idling_at_end)
{
	jack_ringbuffer_t *rb_to_use;

	if(out_to_in_sr_ratio==1.0 || !use_resampling)
	{
		//directly write to rb_resampled_interleaved (skipping rb_interleaved)
		rb_to_use=rb_resampled_interleaved;
	}
	else 
	{
		//write to rb_interleaved
		rb_to_use=rb_interleaved;
	}

	if(jack_ringbuffer_write_space(rb_to_use) < sndfile_request_frames )
	{
		fprintf(stderr,"/!\\ not enough space in ringbuffer\n");
		return 0;
	}

	//frames_read: number of (multi-channel) samples to read, i.e. 1 frame in a stereo file = two values

	//get float frames from any of the readers, requested size ensured to be returned except eof
	frames_read_from_file=read_frames_from_file_to_buffer(frames_read, frames_from_file_buffer);

	//put to the selected ringbuffer
	jack_ringbuffer_write(rb_to_use,(const char*)frames_from_file_buffer,frames_read_from_file*output_port_count*bytes_per_sample);
}


	if(frames_read_from_file>0)
	{
		disk_read_cycle_count++;
		total_bytes_read_from_file+=frames_read_from_file * output_port_count * bytes_per_sample_native;
		total_frames_read_from_file+=frames_read_from_file;
//		fprintf(stderr,"disk_read_frames(): frames: read %"PRId64" total %"PRId64"\n",frames_read_from_file,total_frames_read_from_file);

		if(total_frames_read_from_file>=frame_count)
		{
			if(pause_at_end)//
			{
#ifndef WIN32
				fprintf(stderr,"pae ");
#endif
				total_frames_read_from_file=frame_count;
				all_frames_read=1;//
				is_playing=0;
				is_idling_at_end=1;
			}

			if(loop_enabled)
			{
#ifndef WIN32
				fprintf(stderr,"loop ");
#endif
				total_frames_read_from_file=0;
				all_frames_read=0;
				seek_frames_in_progress=1;
				sf_count_t new_pos=sin_seek(frame_offset,SEEK_SET);
				seek_frames_in_progress=0;
				is_idling_at_end=0;
				return 1;///
			}
		}//end if(total_frames_read_from_file>=frame_count)
		return frames_read_from_file;
	}//end if(frames_read_from_file>0)
	//if no frames were read assume we're at EOF
	all_frames_read=1;
	return 0;
}//end disk_read_frames()

//this method is called from disk_thread (pthread_t)
//it will be called only once and then loop/wait until a condition to finish is met
//=============================================================================
static void *disk_thread_func(void *arg)
{
	//assume soundfile not null

	//seek to given offset position
	sf_count_t count=sin_seek(frame_offset,SEEK_SET);

	//readers read into this buffer, interleaved channels
	frames_from_file_buffer=new float[sndfile_request_frames*output_port_count];

	//===main disk loop
	for(;;)
	{
//		fprintf(stderr,"disk_thread_func() loop\n");

		//check if seek is due
		if(seek_frames_in_progress)
		{
//			fprintf(stderr,"\nseek start === frames to seek %"PRId64"\n",frames_to_seek);
			sf_count_t count=sin_seek(frames_to_seek,frames_to_seek_type);

			seek_frames_in_progress=0;
			frames_to_seek=0;

			sf_count_t new_pos=sin_seek(0,SEEK_CUR);

			total_frames_read_from_file=new_pos-frame_offset;

			//reset some variables before update
			all_frames_read=0;
			is_idling_at_end=0;
			resampling_finished=0;
			if(total_frames_read_from_file>=frame_count)
			{
				all_frames_read=1;
				if(pause_at_end)
				{
#ifndef WIN32
				fprintf(stderr,"pae ");
#endif
					is_idling_at_end=1;
					is_playing=0;
				}

				if(loop_enabled)
				{
#ifndef WIN32
				fprintf(stderr,"loop ");
#endif
					total_frames_read_from_file=0;
					all_frames_read=0;
					seek_frames_in_progress=1;
					sf_count_t new_pos=sin_seek(frame_offset,SEEK_SET);
					seek_frames_in_progress=0;
					is_idling_at_end=0;
				}
			}

//			fprintf(stderr,"\nseek end === new pos %"PRId64" total read %"PRId64"\n",new_pos,total_frames_read_from_file);
		}

		//don't read yet, possibly was started paused or another seek will follow shortly
		if(!is_playing && !is_idling_at_end)
		{
			//===wait here until process() requests to continue
			pthread_cond_wait (&ok_to_read, &disk_thread_lock);
			//once waked up, restart loop
			continue;
		}

		//no resampling needed
		if(out_to_in_sr_ratio==1.0 || !use_resampling)
		{
			if(jack_ringbuffer_write_space(rb_resampled_interleaved)
				< sndfile_request_frames * output_port_count * bytes_per_sample )
			{
				//===wait here until process() requests to continue
				pthread_cond_wait (&ok_to_read, &disk_thread_lock);
				//once waked up, restart loop
				continue;
			}
		}
		else //if out_to_in_sr_ratio!=1.0
		{
			if(jack_ringbuffer_write_space(rb_interleaved)
				< sndfile_request_frames * output_port_count * bytes_per_sample )
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
			if(!is_idling_at_end)
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
//	fprintf(stderr,"disk_thread_func(): disk thread finished\n");

	disk_thread_finished=1;
	return 0;
}//end disk_thread_func()

//=============================================================================
static void setup_disk_thread()
{
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
	if((all_frames_read && !seek_frames_in_progress)
	|| reset_ringbuffers_in_progress)
	{
		return;
	}

//	fprintf(stderr,"req_buffer_from_disk_thread()\n");

	if(jack_ringbuffer_write_space(rb_interleaved) 
		< sndfile_request_frames * output_port_count * bytes_per_sample)
	{
//		fprintf(stderr,"req_buffer_from_disk_thread(): /!\\ not enough write space in rb_interleaved\n");

		return;
	}

	/*
	The pthread_mutex_trylock() function shall be equivalent to pthread_mutex_lock(), 
	except that if the mutex object referenced by mutex is currently locked (by any 
	thread, including the current thread), the call shall return immediately.

	The pthread_mutex_trylock() function shall return zero if a lock on the mutex 
	object referenced by mutex is acquired. Otherwise, an error number is returned 
	to indicate the error. 
	*/
	//if possible to lock the disk_thread_lock mutex
	if(pthread_mutex_trylock (&disk_thread_lock)==0)
	{
//		fprintf(stderr,"new data from disk thread requested\n");
		//signal to disk_thread it can start or continue to read
		pthread_cond_signal (&ok_to_read);
		//unlock again
		pthread_mutex_unlock (&disk_thread_lock);
	}
}//end req_buffer_from_disk_thread()

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

//		fprintf(stderr,"\nexp %d seek_seconds_per_hit %f\n",scale_exponent_seconds,seek_seconds_per_hit);
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

//=============================================================================
//>=0
static void seek_frames_absolute(int64_t frames_abs)
{

	//limit absolute seek to given boundaries (frame_offset, frame_count)
	uint64_t seek_=MAX(frame_offset,frames_abs);
	uint64_t seek =MIN((frame_offset+frame_count),seek_);

	//seek in disk_thread
	frames_to_seek=seek;
	frames_to_seek_type=SEEK_SET;

	reset_ringbuffers();
	seek_frames_in_progress=1;

////need to reset more more
}

//=============================================================================
//+ / -
static void seek_frames(int64_t frames_rel)
{
	if(frames_rel==0)
	{
		//nothing to do
		return;
	}

	//0-seek
	sf_count_t current_read_pos=sin_seek(0,SEEK_CUR);

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

	int64_t seek=0;

	//limit relative seek to given boundaries (frame_offset, frame_count)
	if(frames_rel>0)
	{
		seek=MIN(
			(int64_t)(frame_offset + frame_count - current_read_pos)
			,frames_rel
		);
	}
	else //frames_rel<0
	{
		seek=MAX(

			(int64_t)(frame_offset - current_read_pos)
			,frames_rel
		);
	}

	//seek in disk_thread
	frames_to_seek=seek;
	frames_to_seek_type=SEEK_CUR;

	reset_ringbuffers();

	seek_frames_in_progress=1;

//	fprintf(stderr,"frames to seek %"PRId64"\n",frames_to_seek);

////need to reset more
}

//=============================================================================
static void deinterleave()
{
//	fprintf(stderr,"deinterleave called\n");

	if(all_frames_read && jack_ringbuffer_read_space(rb_resampled_interleaved)==0)
	{
		//nothing to do
//		fprintf(stderr,"deinterleave(): disk thread finished and no more data in rb_resampled_interleaved\n");
		return;
	}

	int resampled_frames_avail=jack_ringbuffer_read_space(rb_resampled_interleaved)/output_port_count/bytes_per_sample;

	//if not limited, deinterleaved block align borked
	int resampled_frames_use=MIN(resampled_frames_avail,jack_period_frames);
//	fprintf(stderr,"deinterleave(): resampled frames avail: %d use: %d\n",resampled_frames_avail,resampled_frames_use);

	//deinterleave from resampled
	if(
		(resampled_frames_use >= 1)
			&&
		(jack_ringbuffer_write_space(rb_deinterleaved) 
			>= jack_period_frames * output_port_count * bytes_per_sample)
	)
	{
//		fprintf(stderr,"deinterleave(): deinterleaving\n");

		void *data_resampled_interleaved;
		data_resampled_interleaved=malloc(resampled_frames_use * output_port_count * bytes_per_sample);

		jack_ringbuffer_read(rb_resampled_interleaved
			,(char*)data_resampled_interleaved
			,resampled_frames_use * output_port_count * bytes_per_sample);

		int bytepos_channel=0;

		for(int channel_loop=0; channel_loop < output_port_count; channel_loop++)
		{
			bytepos_channel=channel_loop * bytes_per_sample;
			int bytepos_frame=0;

			for(int frame_loop=0; frame_loop < resampled_frames_use; frame_loop++)
			{
				bytepos_frame=bytepos_channel + frame_loop * output_port_count * bytes_per_sample;
				//read 1 sample

				float f1=*( (float*)(data_resampled_interleaved + bytepos_frame) );

				//===
				//f1*=0.5;

				if(is_muted)
				{
					f1=0;
				}

				//put to ringbuffer
				jack_ringbuffer_write(rb_deinterleaved
					,(char*)&f1
					,bytes_per_sample);
			}//frame
		}//channel

		free(data_resampled_interleaved);
//		fprintf(stderr,"deinterleave(): done\n");
	}//end if enough data to deinterleave
/*
	else
	{
		fprintf(stderr,"deinterleave(): no deinterleave action in cycle # %"PRId64". frames resampled read space %d deinterleaved write space %d\n"
			,process_cycle_count
			,jack_ringbuffer_read_space(rb_resampled_interleaved) / output_port_count / bytes_per_sample
			,jack_ringbuffer_write_space(rb_deinterleaved) / output_port_count / bytes_per_sample );

	}
*/
}//end deinterleave()

//=============================================================================
static void print_stats()
{
	if(!debug)
	{
		return;
	}

	fprintf(stderr,"-stats: proc cycles %"PRId64" read cycles %"PRId64" proc underruns %"PRId64" bytes from file %"PRId64"\n-stats: frames: from file %"PRId64" input resampled %"PRId64" pushed to JACK %"PRId64"\n-stats: interleaved %lu resampled %lu deinterleaved %lu resampling finished %d all frames read %d disk thread finished %d\n"
		,process_cycle_count
		,disk_read_cycle_count
		,process_cycle_underruns
		,total_bytes_read_from_file

		,total_frames_read_from_file
		,total_input_frames_resampled
		,total_frames_pushed_to_jack

		,jack_ringbuffer_read_space(rb_interleaved)          /output_port_count/bytes_per_sample
		,jack_ringbuffer_read_space(rb_resampled_interleaved)/output_port_count/bytes_per_sample
		,jack_ringbuffer_read_space(rb_deinterleaved)        /output_port_count/bytes_per_sample

		,resampling_finished
		,all_frames_read
		,disk_thread_finished
	);
}//end print_stats()

//=============================================================================
static void signal_handler(int sig)
{
	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
//	fprintf(stderr,"signal_handler() called\n");

	print_stats();

	if(process_cycle_underruns>0)
	{
		fprintf(stderr,"/!\\ underruns: %"PRId64"\n",process_cycle_underruns);
	}

//	fprintf(stderr,"expected frames pushed to JACK (excl. resampler padding): %f\n",(double)(frame_count * out_to_in_sr_ratio) );

	if(sig!=42)
	{
//		fprintf(stderr, "terminate signal %d received\n",sig);
	}

	if(client!=NULL)
	{
		jack_deactivate(client);
//		fprintf(stderr,"JACK client deactivated. ");

		int index=0;
		while(ioPortArray[index]!=NULL && index<output_port_count)
		{
			jack_port_unregister(client,ioPortArray[index]);
			index++;
		}

//		fprintf(stderr,"JACK ports unregistered\n");

		jack_client_close(client);
//		fprintf(stderr,"JACK client closed\n");
	}

	sin_close();
//	fprintf(stderr,"soundfile closed\n");
	if(is_mpg123)
	{
		mpg123_exit();
	}

	free_ringbuffers();

	reset_terminal();

	fprintf(stderr,"jack_playfile done.\n");
	exit(0);
}//end signal_handler()

//EOF
