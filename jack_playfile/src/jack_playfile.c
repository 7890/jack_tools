#include "jack_playfile.h"

//tb/150612+

//simple file player for JACK
//inspired by jack_play, libsndfile, zresampler


//command line arguments
//======================
static const char *filename=NULL; //mandatory

//start from absolute frame pos (skip n frames from start)
static uint64_t frame_offset=0; //optional

//number of frames to read & play from offset (if argument not provided or 0: all frames)
static uint64_t frame_count=0; //optional, only in combination with offset
//======================

//if set to 0, will not resample, even if file has different SR from JACK
static int use_resampling=1;

//if set to 1, connect available file channels to available physical outputs
static int autoconnect_jack_ports=1;

//prepare everything for playing but wait for user to toggle to play
static int start_paused=0;

//don't quit program when everything has played out
////////********
static int pause_when_finished=0;

//if set to 1, will print stats
static int debug=0;

//if set to 1, will add "sample markers" for visual debugging in sisco
static int add_markers=0;
static float marker_first_sample_normal_jack_period=0.2;
static float marker_last_sample_out_of_resampler=-0.5;
static float marker_first_sample_last_jack_period=0.9;
static float marker_pad_samples_last_jack_period=-0.2;
static float marker_last_sample_last_jack_period=-0.9;

//how many ports the JACK client will have
//for now: will use the same count as file has channels
static int output_port_count=0;

//sndfile will read float, 4 bytes per sample
//good to use with JACK buffers 
static int bytes_per_sample=4;//==sizeof(sample_t);

//set after connection to JACK succeeded
static int jack_sample_rate=0;
static int jack_period_frames=0;
static float jack_cycles_per_second=0;

//how many bytes one sample (of one channel) is using in the file
static int bytes_per_sample_native=0;

//array of pointers to JACK input or output ports
static jack_port_t **ioPortArray;
static jack_client_t *client;
static jack_options_t jack_opts = JackNoStartServer;

//process() will return immediately if 0
static int process_enabled=0;
static int shutdown_in_progress=0;
static int shutdown_in_progress_signalled=0;

//disk thread
static pthread_t disk_thread={0};
static pthread_mutex_t disk_thread_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ok_to_read=PTHREAD_COND_INITIALIZER;
static int disk_thread_finished=0;

//handle to currently playing file
static SNDFILE *soundfile=NULL;

//holding basic audio file properties
static SF_INFO sf_info;

//reported by stat()
static uint64_t file_size_bytes=0;

//derive from sndfile infos or estimate based on filesize & framecount
float file_data_rate_bytes_per_second=0;

//JACK output bytes per second
float jack_output_data_rate_bytes_per_second=0;

//JACK output to file input byte ratio
float out_to_in_byte_ratio=0;

//JACK sr to file sr ratio
static double out_to_in_sr_ratio=1;

//zita-resampler
static Resampler R;

//quality (higher means better means more cpu)
//valid range 16 to 96
int RESAMPLER_FILTERSIZE=64;

//ringbuffers
//read from file, write to rb_interleaved (in case of resampling)
static jack_ringbuffer_t *rb_interleaved=NULL;
//read from file, write (directly) to rb_resampled_interleaved (in case of no resampling), rb_interleaved unused/skipped
//read from rb_interleaved, resample and write to rb_resampled_interleaved (in case of resampling)
static jack_ringbuffer_t *rb_resampled_interleaved=NULL;
//read from rb_resampled_interleaved, write to rb_deinterleaved
static jack_ringbuffer_t *rb_deinterleaved=NULL;
//read from rb_deinterleaved, write to jack output buffers in JACK process()

//how many frames to read per request
static int sndfile_request_frames=0;

//counters
//JACK process cycles (not counted if process_enabled==0 or shutdown_in_progress==1)
//first cycle indicated as 1
static uint64_t process_cycle_count=0;

//disk reads
//first cycle indicated as 1
static uint64_t disk_read_cycle_count=0;

//counter for process() cycles where ringbuffer didn't have enough data for one cycle
//(one full period for every channel)
static uint64_t process_cycle_underruns=0;

static uint64_t total_bytes_read_from_file=0;

static uint64_t total_frames_read_from_file=0;

static uint64_t total_frames_pushed_to_jack=0;

//in disk thread, detect if 'frame_count' was read from file
static int all_frames_read=0;

//in resample(), detect when all requested frames were resampled
static int resampling_finished=0;

//frames put to resampler, without start/end padding
static uint64_t total_input_frames_resampled=0;

//toggle play/pause with 'space'
static int is_playing=1;

//toggle mute with 'm'
static int is_muted=0;

//toggle loop with 'l'
static int loop_enabled=0; //unused

//if set to 0, keyboard entry won't be used (except ctrl+c)
static int keyboard_control_enabled=1;

//0: frames, 1: seconds
static int is_time_seconds=1;

//0: relative to frame_offset and frame_offset + frame_count
//1: relative to frame 0
static int is_time_absolute=0;

//0: time remaining (-), 1: time elapsed
static int is_time_elapsed=1;

//arrows left and right, home, end
static int seek_frames_in_progress=0;

//relative seek, how many (native) frames
uint64_t seek_frames_per_hit=0;

#ifndef WIN32
	static struct termios initial_settings; //cooked
	struct termios settings; //raw

	//lower values mean faster repetition of events from held key (~ ?)
	#define MAGIC_MAX_CHARS 5//18
	static unsigned char keycodes[ MAGIC_MAX_CHARS ];
#else
	DWORD        w_term_mode;
	HANDLE       w_term_hstdin;
	INPUT_RECORD w_term_inrec;
	DWORD        w_term_count;
#endif

static int KEY_SPACE=0;
static int KEY_Q=0;
static int KEY_H=0;
static int KEY_F1=0;
static int KEY_ARROW_LEFT=0;
static int KEY_ARROW_RIGHT=0;
static int KEY_ARROW_UP=0;
static int KEY_ARROW_DOWN=0;
static int KEY_HOME=0;
static int KEY_END=0;
static int KEY_BACKSPACE=0;
static int KEY_M=0;
static int KEY_L=0;
static int KEY_COMMA=0;
static int KEY_PERIOD=0;
static int KEY_DASH=0;

static const char *clear_to_eol_seq=NULL;
static const char *turn_on_cursor_seq=NULL;
static const char *turn_off_cursor_seq=NULL;

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
static void print_stats()
{
	if(!debug)
	{
		return;
	}

	fprintf(stderr,"-stats: proc cycles %"PRId64" read cycles %"PRId64" proc underruns %"PRId64" bytes from file %"PRId64"\n-stats: frames: from file %"PRId64" input resampled %"PRId64" pushed to JACK %"PRId64"\n-stats: interleaved %"PRId64" resampled %"PRId64" deinterleaved %"PRId64" resampling finished %d all frames read %d disk thread finished %d\n"
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
}

//=============================================================================
int main(int argc, char *argv[])
{
	fprintf(stderr,"*** jack_playfile ALPHA - protect your ears! ***\n");
	if( argc < 2	
		|| (argc >= 2 && 
			( strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	)
	{
		fprintf(stderr,"jack_playfile v%1.1f - (c) 2015 Thomas Brand <tom@trellis.ch>\n\n",version);
		fprintf(stderr,"syntax: jack_playfile <file> [frame offset [frame count]]\n\n");
		exit(0);
	}
	else if (argc >= 2)
	{
		filename=argv[1];
	}

	if (argc >= 3)
	{
		frame_offset=atoi(argv[2]);
	}

	if (argc >= 4)
	{
		frame_count=atoi(argv[3]);
	}

	memset (&sf_info, 0, sizeof (sf_info)) ;
	/*
	typedef struct
	{ sf_count_t  frames ; //Used to be called samples.
	  int         samplerate ;
	  int         channels ;
	  int         format ;
	  int         sections ;
	  int         seekable ;
        } SF_INFO ;
	*/

	//init soundfile
	soundfile=sf_open(filename,SFM_READ,&sf_info);
	if(soundfile==NULL)
	{
		fprintf (stderr, "/!\\ cannot open file \"%s\"\n(%s)\n", filename, sf_strerror(NULL));
		exit(1);
	}
	//sf_close (soundfile);

	struct stat st;
	stat(filename, &st);
	file_size_bytes = st.st_size;

	fprintf(stderr,"file:        %s\n",filename);
	fprintf(stderr,"size:        %"PRId64" bytes (%.2f MB)\n",file_size_bytes,(float)file_size_bytes/1000000);

	//for now: just create as many JACK client output ports as the file has channels
	output_port_count=sf_info.channels;

	if(output_port_count<=0)
	{
		fprintf(stderr,"/!\\ file has zero channels, nothing to play!\n");
		exit(1);
	}

	bytes_per_sample_native=file_info(sf_info,1);

	if(bytes_per_sample_native<=0)
	{
		//try estimation: total filesize (including headers, other chunks ...) divided by (frames*channels*native bytes)
		file_data_rate_bytes_per_second=(float)file_size_bytes
			/get_seconds(&sf_info);

		fprintf(stderr,"data rate:   %.1f bytes/s (%.2f MB/s) average, estimated\n"
			,file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
	}
	else
	{
		file_data_rate_bytes_per_second=sf_info.samplerate * output_port_count * bytes_per_sample_native;
		fprintf(stderr,"data rate:   %.1f bytes/s (%.2f MB/s)\n",file_data_rate_bytes_per_second,(file_data_rate_bytes_per_second/1000000));
	}

	if( (file_data_rate_bytes_per_second/1000000) > 20 )
	{
		fprintf(stderr,"/!\\ this is a relatively high data rate\n");
	}

	//offset can't be negative or greater total frames in file
	if(frame_offset<0 || frame_offset>sf_info.frames)
	{
		frame_offset=0;
		fprintf(stderr,"frame_offset set to %"PRId64"\n",frame_offset);
	}

	//if requested count negative, zero or greater total frames in file
	if(frame_count<=0 || frame_count>sf_info.frames)
	{
		//set possible max respecting frame_offset
		frame_count=sf_info.frames-frame_offset;
		fprintf(stderr,"frame_count set to %"PRId64"",frame_count);
		if(frame_count==sf_info.frames)
		{
			fprintf(stderr," (all available frames)");
		}
		fprintf(stderr,"\n");
	}

	//offset + count can't be greater than frames in file
	if( (frame_offset+frame_count) > sf_info.frames)
	{
		//set possible max respecting frame_offset
		frame_count=MIN((sf_info.frames-frame_offset),frame_count);

		fprintf(stderr,"frame_count set to %"PRId64"\n",frame_count);
	}

	fprintf(stderr,"playing frames from/to/length: %"PRId64" %"PRId64" %"PRId64"\n"
		,frame_offset
		,MIN(sf_info.frames,frame_offset+frame_count)
		,frame_count);

	//~1%
	seek_frames_per_hit=ceil(frame_count / 100);
//	fprintf(stderr,"seek frames %"PRId64"\n",seek_frames_per_hit);

	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?).\nthis is fatal: jack_playfile needs JACK to run.\n");
		fprintf(stderr,"see http://jackaudio.org for more information on the JACK Audio Connection Kit.\n");
		exit(1);
	}

	const char **ports;
	jack_status_t status;

	//===
	const char *client_name="jack_playfile";

	//create an array of output ports
	ioPortArray = (jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, jack_opts, &status, NULL);

	if (client == NULL) 
	{
		fprintf (stderr, "/!\\ jack_client_open() failed, status = 0x%2.0x\n", status);
		exit(1);
	}

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

	/*
	downsampling (ratio>1)
	in: 10
	out: 2
	out_to_in: 10/2 = 5
	read 10 samples for 2 output samples
	read 5 samples for 1 output sample

	upsampling (ratio<1)
	in: 2
	out: 10
	out_to_in: 2/10 = 0.2
	read two samples for 10 output samples
	read 0.2 samples for 1 output sample

	file 44100, jack 96000 -> 0.459375
	for one jack_period_frames, we need at least period_size * out_to_in_sr_ratio frames from input
	*/

	//sampling rate ratio output (JACK) to input (file)
	out_to_in_sr_ratio=(double)jack_sample_rate/sf_info.samplerate;

	//ceil: request a bit more than needed to satisfy ratio
	//will result in inp_count>0 after process ("too much" input for out/in ratio, will always have output)

	if(use_resampling)
	{
		sndfile_request_frames=ceil(jack_period_frames * (double)1/out_to_in_sr_ratio);
	}
	else
	{
		sndfile_request_frames=jack_period_frames;
	}

	///
	int rb_interleaved_size_bytes		=100 * sndfile_request_frames    * output_port_count * bytes_per_sample;
	int rb_resampled_interleaved_size_bytes	=100 * jack_period_frames        * output_port_count * bytes_per_sample;
	int rb_deinterleaved_size_bytes		=100 * jack_period_frames        * output_port_count * bytes_per_sample;

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

	setup_resampler();

	setup_disk_thread();

	jack_set_process_callback (client, process, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);

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

	//request first chunk from file
	req_buffer_from_disk_thread();

	//now activate client in JACK, starting with process() cycles
	if (jack_activate (client)) 
	{
		fprintf (stderr, "/!\\ cannot activate client\n\n");
		free_ringbuffers();
		exit(1);
	}

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
	if(false)
	{
		const char *left_out= "Simple Scope (Stereo) GTK:in1";
		const char *right_out="Simple Scope (Stereo) GTK:in2";
		jack_connect (client, jack_port_name(ioPortArray[0]) , left_out);
		jack_connect (client, jack_port_name(ioPortArray[1]) , right_out);
		//override
		autoconnect_jack_ports=0;
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

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	init_term_seq();

	init_key_codes();

	//now set raw to read key hits
	set_terminal_raw();

	if(start_paused)
	{
		is_playing=0;
	}

	process_enabled=1;
//	fprintf(stderr,"process_enabled\n");

	//run until interrupted
	while (1) 
	{

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

			if(is_playing)
			{
				fprintf(stderr,">  playing  ");
			}
			else
			{
				fprintf(stderr,"|| paused   ");
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
				fprintf(stderr,"L  ");
			}
			else
			{
				fprintf(stderr,"   ");
			}

			print_clock();

			handle_key_hits();
		}
#ifdef WIN32
		Sleep(100);
#else
		usleep(10000);
#endif
	}
	exit(0);
}//end main

//=============================================================================
static void print_clock()
{
	sf_count_t pos=sf_seek(soundfile,0,SEEK_CUR);;
	double seconds=0;

/*

 elapsed                     remaining
|--------------------------|------------------|  abs

               elapsed       rem
             |-------------|-----|
                           v
|------------|-------------------|------------|  rel
             off                 off+count
*/

	if(!is_time_absolute)
	{
		pos-=frame_offset;

		if(!is_time_elapsed)
		{
			pos=frame_count-pos;
		}
	}
	else //absolute
	{
		if(!is_time_elapsed)
		{
			pos=sf_info.frames-pos;
		}
	}

	seconds=frames_to_seconds(pos,sf_info.samplerate);

	if(is_time_seconds)
	{
		fprintf(stderr,"S %s %s %9.1f %s(%s) "
			,(is_time_absolute ? "abs" : "rel")
			,(is_time_elapsed ? " " : "-")
			,frames_to_seconds(pos,sf_info.samplerate)
			,(is_time_elapsed ? " " : "-")
			,format_duration_str(seconds));
	}
	else
	{
		fprintf(stderr,"F %s %s %9"PRId64" %s(%s) "
			,(is_time_absolute ? "abs" : "rel")
			,(is_time_elapsed ? " " : "-")
			,pos
			,(is_time_elapsed ? " " : "-")
			,format_duration_str(seconds));
	}
}
//=============================================================================
static void handle_key_hits()
{
/*
>  playing   ML  1234.5 (0:12:34.1)  << (``)  
^            ^^  ^       ^           ^  ^
*/
	int rawkey=read_raw_key();
//	fprintf(stderr,"rawkey: %d\n",rawkey);

	//no key while timeout not reached
	if(rawkey==0)
	{
		//clear
//		fprintf(stderr,"%s",clear_to_eol_seq);
	}

	//'space': toggle play/pause
	else if(rawkey==KEY_SPACE)
	{
		is_playing=!is_playing;
	}
	//'q': quit
	else if(rawkey==KEY_Q)
	{
		fprintf(stderr,"\r%s\rquit received\n",clear_to_eol_seq);
		shutdown_in_progress=1;
	}
	//'h' or 'f1': help
	else if(rawkey==KEY_H || rawkey==KEY_F1)
	{
		print_keyboard_shortcuts();
	}
	//'<' (arrow left): 
	else if(rawkey==KEY_ARROW_LEFT)
	{
		fprintf(stderr,"<< ");
		print_next_wheel_state(-1);
		seek_frames(-seek_frames_per_hit);
	}
	//'>' (arrow right): 
	else if(rawkey==KEY_ARROW_RIGHT)
	{
		fprintf(stderr,">> ");
		print_next_wheel_state(+1);
		seek_frames( seek_frames_per_hit);
	}
	//'^' (arrow up):
	else if(rawkey==KEY_ARROW_UP)
	{
		fprintf(stderr,"^^ ");
		print_next_wheel_state(1);
//unused
	}
	//'v' (arrow down):
	else if(rawkey==KEY_ARROW_DOWN)
	{
		fprintf(stderr,"vv ");
		print_next_wheel_state(-1);
//unused
	}
	//'|<' (home, backspace):
	else if(rawkey==KEY_HOME || rawkey==KEY_BACKSPACE)
	{
		fprintf(stderr,"|< home ");
		seek_frames_absolute(frame_offset);
	}
	//'>|' (end):
	else if(rawkey==KEY_END)
	{
		fprintf(stderr,">| end ");
		seek_frames_absolute(frame_offset+frame_count);
	}
	//'m': toggle mute
	else if(rawkey==KEY_M)
	{
		is_muted=!is_muted;
		fprintf(stderr,"mute %s",is_muted ? "on " : "off ");
	}
	//'l': loop
	else if(rawkey==KEY_L)
	{
		loop_enabled=!loop_enabled;
		fprintf(stderr,"loop %s",loop_enabled ? "on " : "off ");
	}
	//',': 
	else if(rawkey==KEY_COMMA)
	{
		is_time_seconds=!is_time_seconds;
		fprintf(stderr,"time %s",is_time_seconds ? "seconds " : "frames ");
	}
	//'.': 
	else if(rawkey==KEY_PERIOD)
	{
		is_time_absolute=!is_time_absolute;
		fprintf(stderr,"time %s",is_time_absolute ? "absolute " : "relative ");
	}
	//'-': 
	else if(rawkey==KEY_DASH)
	{
		is_time_elapsed=!is_time_elapsed;
		fprintf(stderr,"time %s",is_time_elapsed ? "elapsed " : "remaining ");
	}

#ifndef WIN32
		fprintf(stderr,"%s",clear_to_eol_seq);
#else
		fprintf(stderr,"                 ");
#endif

}//end handle_key_hits()

//=============================================================================
static void print_keyboard_shortcuts()
{
	fprintf(stderr,"\r%s\rkeyboard shortcuts:\n",clear_to_eol_seq);

	fprintf(stderr,"  h, f1:             help (this screen)\n");
	fprintf(stderr,"  space:             toggle play/pause\n");
	fprintf(stderr,"  < left:            seek backward (1%%)\n");
	fprintf(stderr,"  > right:           seek forward (1%%)\n");
	fprintf(stderr,"  home, backspace:   seek to start\n");
	fprintf(stderr,"  end:               seek to end\n");
	fprintf(stderr,"  m:                 mute\n");
	fprintf(stderr,"  l:                 loop\n");
	fprintf(stderr,"  , comma:           toggle clock seconds*  / frames\n");
	fprintf(stderr,"  . period:          toggle clock absolute* / relative\n");
	fprintf(stderr,"  - dash:            toggle clock elapsed*  / remaining\n");
	fprintf(stderr,"  q:                 quit\n\n");
}//end print_keyboard_shortcuts()

//=============================================================================
static int process(jack_nframes_t nframes, void *arg) 
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
			if(is_playing)
			{
				//read more data
				req_buffer_from_disk_thread();
			}
		}
	}

	resample();
	deinterleave();

	if(!is_playing || seek_frames_in_progress)
	{
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;
			//get output buffer from JACK for that channel
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],jack_period_frames);
			//set all samples zero
			memset(o1, 0, jack_period_frames*bytes_per_sample);
		}
		return 0;
	}

	if(all_frames_read
		&& jack_ringbuffer_read_space(rb_interleaved)==0
		&& jack_ringbuffer_read_space(rb_resampled_interleaved)==0
		&& jack_ringbuffer_read_space(rb_deinterleaved)==0)
	{
//		fprintf(stderr,"process(): diskthread finished and no more data in rb_interleaved, rb_resampled_interleaved, rb_deinterleaved\n");
//		fprintf(stderr,"process(): shutdown condition 1 met\n");

		//fill buffers with silence (last cycle before shutdown (?))
		for(int i=0; i<output_port_count; i++)
		{
			sample_t *o1;
			//get output buffer from JACK for that channel
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],jack_period_frames);
			//set all samples zero
			memset(o1, 0, jack_period_frames*bytes_per_sample);
		}

		shutdown_in_progress=1;
		return 0;
	}

	//count at start of enabled cycles (1st cycle = #1)
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

//		fprintf(stderr,"process(): /!\\ ======== underrun\n");

		print_stats();
		return 0;
	}

	req_buffer_from_disk_thread();

	return 0;
}//end process()

//=============================================================================
//>=0
static void seek_frames_absolute(int64_t frames_abs)
{
	seek_frames_in_progress=1;

	reset_ringbuffers();

	//limit absolute seek to given boundaries (frame_offset, frame_count)
	uint64_t seek_=MAX(frame_offset,frames_abs);
	uint64_t seek =MIN((frame_offset+frame_count),seek_);

	sf_count_t new_pos=sf_seek(soundfile,seek,SEEK_SET);

////////////////
//need to reset more
total_frames_read_from_file=new_pos-frame_offset;

//	fprintf(stderr,"\nseek %"PRId64" new pos %"PRId64"\n",seek,count);

//	req_buffer_from_disk_thread();

	//in process()
	//seek_frames_in_progress=0;
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

	seek_frames_in_progress=1;

	reset_ringbuffers();

/*
                            current abs pos    
         abs start          v                                   abs end
         |------------------------------------------------------|
                     |--------------------------|
                     frame_offset               offset + frame_count


                     |-----|--------------------|

                     max <   max >

              .======x======.=============.=====x=======.
                     |      seek steps          |
                     limit                      limit
*/


	//0-seek
	sf_count_t current_read_pos=sf_seek(soundfile,0,SEEK_CUR);

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

	sf_count_t new_pos=sf_seek(soundfile,seek,SEEK_CUR);

////////////////
//need to reset more
	total_frames_read_from_file=new_pos-frame_offset;

//	fprintf(stderr,"cur pos %"PRId64" seek %"PRId64" new pos %"PRId64"\n",current_read_pos,seek,new_pos);

//	req_buffer_from_disk_thread();
	
	//in process()
	//seek_frames_in_progress=0;
}

//=============================================================================
static void setup_resampler()
{
	//test if resampling needed
	if(out_to_in_sr_ratio!=1)//sf_info.samplerate!=jack_sample_rate)
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
			if (R.setup (sf_info.samplerate, jack_sample_rate, sf_info.channels, RESAMPLER_FILTERSIZE))
			{
				fprintf (stderr, "/!\\ sample rate ratio %d/%d is not supported.\n"
					,jack_sample_rate,sf_info.samplerate);
				use_resampling=0;
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
					,sf_info.samplerate
					,jack_sample_rate
					,out_to_in_sr_ratio);
*/

				fprintf(stderr,"resampler out_to_in ratio: %f\n",out_to_in_sr_ratio);

			}//end resampler setup
 		}//end if use_resampling
		else
		{
			fprintf(stderr,"will play file without resampling.\n");
		}
	}//end unequal in/out sr
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
		>= sndfile_request_frames * output_port_count * bytes_per_sample)
	{

//		fprintf(stderr,"resample(): normal operation\n");

		float *interleaved_frame_buffer=new float [sndfile_request_frames * output_port_count];
		float *buffer_resampling_out=new float [jack_period_frames * output_port_count];

		//condition to jump into while loop
		R.out_count=1;

		int resampler_loop_counter=0;
		while(R.out_count>0)
		{
			//read from rb_interleaved, just peek / don't move read pointer yet
			jack_ringbuffer_peek(rb_interleaved
				,(char*)interleaved_frame_buffer
				,sndfile_request_frames * output_port_count * bytes_per_sample);
			
			//configure for next resampler process cycle
			R.inp_data=interleaved_frame_buffer;
			R.inp_count=sndfile_request_frames;
			R.out_data=buffer_resampling_out;
			R.out_count=jack_period_frames;

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
				,(sndfile_request_frames-R.inp_count) * output_port_count * bytes_per_sample);

			total_input_frames_resampled+=(sndfile_request_frames-R.inp_count);

			resampler_loop_counter++;
		}//end while(R.out_count>0)

		//finally write resampler output to rb_resampled_interleaved
		jack_ringbuffer_write(rb_resampled_interleaved
			,(const char*)buffer_resampling_out
			,jack_period_frames * output_port_count * bytes_per_sample);

		delete[] interleaved_frame_buffer;
		delete[] buffer_resampling_out;
	}

	//finished with partial or no data left, feed zeroes at end
	else if(all_frames_read && jack_ringbuffer_read_space(rb_interleaved)>=0)
	{
		int frames_left=jack_ringbuffer_read_space(rb_interleaved)/output_port_count/bytes_per_sample;

//		fprintf(stderr,"resample(): partial data in rb_interleaved (frames): %d\n",frames_left);

		//adding zero pad to get full output of resampler

////////////////////
		int final_frames=frames_left + get_resampler_pad_size_end();

		float *interleaved_frame_buffer=new float [ final_frames  * output_port_count];
		float *buffer_resampling_out=new float [ ( (int)(out_to_in_sr_ratio * final_frames) ) * output_port_count ];

		//read from rb_interleaved
		jack_ringbuffer_read(rb_interleaved
			,(char*)interleaved_frame_buffer
			,frames_left * output_port_count * bytes_per_sample);
			
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
			int last_sample_index=( (int)(out_to_in_sr_ratio * final_frames) ) * output_port_count - output_port_count;

			//mark last samples of all channels
			for(int i=0;i<output_port_count;i++)
			{
				buffer_resampling_out[last_sample_index+i]  =marker_last_sample_out_of_resampler;
			}
		}

		//finally write resampler output to rb_resampled_interleaved
		jack_ringbuffer_write(rb_resampled_interleaved
			,(const char*)buffer_resampling_out
			,(int)(out_to_in_sr_ratio * final_frames) * output_port_count * bytes_per_sample);

////////////////////
//		delete[] interleaved_frame_buffer;
//		delete[] buffer_resampling_out;

		resampling_finished=1;
	}
	else
	{
////////////////////
//		fprintf(stderr,"/!\\ this should not happen\n");
	}

	print_stats();

}//end resample()

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
static int disk_read_frames(SNDFILE *soundfile_)
{
//	fprintf(stderr,"disk_read_frames() called\n");

	if(total_frames_read_from_file>=frame_count)
	{
		if(loop_enabled)
		{
			total_frames_read_from_file=0;//frame_offset;

			sf_count_t new_pos=sf_seek(soundfile,frame_offset,SEEK_SET);
		}
		else
		{
			all_frames_read=1;
			return 0;
		}
	}

	uint64_t frames_to_go=frame_count-total_frames_read_from_file;
//	fprintf(stderr,"disk_read_frames(): frames to go %" PRId64 "\n",frames_to_go);

	sf_count_t frames_read_from_file=0;
	int buf_avail=0;
	jack_ringbuffer_data_t write_vec[2];

	if(out_to_in_sr_ratio==1.0 || !use_resampling)
	{
		//directly write to rb_resampled_interleaved (skipping rb_interleaved)
		jack_ringbuffer_get_write_vector (rb_resampled_interleaved, write_vec) ;
	}
	else 
	{
		//write to rb_interleaved
		jack_ringbuffer_get_write_vector (rb_interleaved, write_vec) ;
	}

	//common
	//only read/write as many frames as requested (frame_count)
	//respecting available split buffer lengths and given read chunk size (sndfile_request_frames)
	int frames_read_=0;
	int frames_read =0;

	if (write_vec [0].len)
	{
		buf_avail = write_vec [0].len / output_port_count / bytes_per_sample ;

		frames_read_=MIN(frames_to_go,sndfile_request_frames);
		frames_read =MIN(frames_read_,buf_avail);

		//fill the first part of the ringbuffer
		frames_read_from_file = sf_readf_float (soundfile_, (float *) write_vec [0].buf, frames_read) ;

		frames_to_go-=frames_read_from_file;

		if (write_vec [1].len && frames_to_go>0)
		{
			buf_avail = write_vec [1].len / output_port_count / bytes_per_sample ;

			frames_read_=MIN(frames_to_go,sndfile_request_frames-frames_read_from_file);
			frames_read=MIN(frames_read_,buf_avail);

			//fill the second part of the ringbuffer
			frames_read_from_file += sf_readf_float (soundfile_, (float *) write_vec [1].buf, frames_read);
		}
	}
	else
	{
		fprintf(stderr,"disk_read_frames(): /!\\ can not write to ringbuffer\n");
	}

	if (frames_read_from_file > 0)
	{
		disk_read_cycle_count++;

		total_bytes_read_from_file+=frames_read_from_file * output_port_count * bytes_per_sample_native;

		total_frames_read_from_file+=frames_read_from_file;

//		fprintf(stderr,"disk_read_frames(): frames: read %"PRId64" total %"PRId64"\n",frames_read_from_file,total_frames_read_from_file);

		//advance write pointers
		if(out_to_in_sr_ratio==1.0 || !use_resampling)
		{
			jack_ringbuffer_write_advance (rb_resampled_interleaved
				,frames_read_from_file * output_port_count * bytes_per_sample) ;
		}
		else 
		{
			jack_ringbuffer_write_advance (rb_interleaved
				,frames_read_from_file * output_port_count * bytes_per_sample) ;
		}

		if(total_frames_read_from_file>=frame_count)
		{
//			fprintf(stderr,"disk_read_frame(): all frames read from file\n");

			if(loop_enabled)
			{
#ifndef WIN32
				fprintf(stderr,"loop");
#endif
				total_frames_read_from_file=0;//frame_offset;

				sf_count_t new_pos=sf_seek(soundfile,frame_offset,SEEK_SET);
			}
			else
			{
				all_frames_read=1;
			}
		}

		return frames_read_from_file;
	}
	//if no frames were read assume we're at EOF

	all_frames_read=1;

	return 0;
}//end disk_read_frames()

//this method is called from disk_thread (pthread_t)
//it will be called only once and then loop/wait until a condition to finish is met
//=============================================================================
static void *disk_thread_func(void *arg)
{
	if(soundfile==NULL)
	{
		fprintf (stderr, "\n/!\\ cannot open sndfile \"%s\" for input (%s)\n", filename,sf_strerror(NULL));
		jack_client_close(client);
		exit(1);
	}

	//seek to given offset position
	sf_count_t count=sf_seek(soundfile,frame_offset,SEEK_SET);

	//===main disk loop
	for(;;)
	{
//		fprintf(stderr,"disk_thread_func() loop\n");

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
		if(!disk_read_frames(soundfile))
		{
			//disk_read() returns 0 on EOF
			goto done;
		}

		//===wait here until process() requests to continue
		pthread_cond_wait (&ok_to_read, &disk_thread_lock);
	}//end main loop
done:
	sf_close(soundfile);

	pthread_mutex_unlock (&disk_thread_lock);
//	fprintf(stderr,"disk_thread_func(): disk thread finished\n");

	disk_thread_finished=1;
	return 0;
}//end disk_thread_func()

//=============================================================================
static void setup_disk_thread()
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//initially lock
	pthread_mutex_lock(&disk_thread_lock);
	//create the disk_thread (pthread_t) with start routine disk_thread_func
	//disk_thread_func will be called after thread creation
	//(not attributes, no args)
	pthread_create(&disk_thread, NULL, disk_thread_func, NULL);
//	fprintf(stderr,"disk thread started\n");
}

//=============================================================================
static void req_buffer_from_disk_thread()
{
	if(all_frames_read)
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
static void free_ringbuffers()
{
//	fprintf(stderr,"releasing ringbuffers\n");
	jack_ringbuffer_free(rb_interleaved);
	jack_ringbuffer_free(rb_resampled_interleaved);
	jack_ringbuffer_free(rb_deinterleaved);
}

//=============================================================================
static void reset_ringbuffers()
{
//	fprintf(stderr,"releasing ringbuffers\n");
	jack_ringbuffer_reset(rb_deinterleaved);
	jack_ringbuffer_reset(rb_resampled_interleaved);
	jack_ringbuffer_reset(rb_interleaved);
}

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
		fprintf(stderr, "terminate signal %d received\n",sig);
	}

	jack_deactivate(client);
//	fprintf(stderr,"JACK client deactivated. ");

	int index=0;
	while(ioPortArray[index]!=NULL && index<output_port_count)
	{
		jack_port_unregister(client,ioPortArray[index]);
		index++;
	}

//	fprintf(stderr,"JACK ports unregistered\n");

	jack_client_close(client);
//	fprintf(stderr,"JACK client closed\n");

	//close soundfile
	if(soundfile!=NULL)
	{
		sf_close (soundfile);
//		fprintf(stderr,"soundfile closed\n");
	}

	free_ringbuffers();

	reset_terminal();

	fprintf(stderr,"jack_playfile done.\n");
	exit(0);
}//end signal_handler()

//=============================================================================
static void jack_shutdown_handler (void *arg)
{
	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr, "/!\\ JACK server down!\n");

	//close soundfile
	if(soundfile!=NULL)
	{
		sf_close (soundfile);
//		fprintf(stderr,"soundfile closed\n");
	}

	free_ringbuffers();

	reset_terminal();

	exit(1);	
}

//=============================================================================
//http://www.cplusplus.com/forum/articles/7312/#msg33734
static void set_terminal_raw()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//save original tty settings ("cooked")
	tcgetattr( STDIN_FILENO, &initial_settings );

	tcgetattr( STDIN_FILENO, &settings );

	//set the console mode to no-echo, raw input
	settings.c_cc[ VTIME ] = 1;
	settings.c_cc[ VMIN  ] = MAGIC_MAX_CHARS;
	settings.c_iflag &= ~(IXOFF);
	settings.c_lflag &= ~(ECHO | ICANON);
	tcsetattr( STDIN_FILENO, TCSANOW, &settings );

	//turn off cursor
	fprintf(stderr,"%s",turn_off_cursor_seq);//

	//in shutdown signal handler
	//tcsetattr( STDIN_FILENO, TCSANOW, &initial_settings );
#else
	//set the console mode to no-echo, raw input, and no window or mouse events
	w_term_hstdin = GetStdHandle( STD_INPUT_HANDLE );
	if (w_term_hstdin == INVALID_HANDLE_VALUE
		|| !GetConsoleMode( w_term_hstdin, &w_term_mode )
		|| !SetConsoleMode( w_term_hstdin, 0 ))
	{
		fprintf(stderr,"/!\\ could not initialize terminal\n");
		return;
	}
	FlushConsoleInputBuffer( w_term_hstdin );

	//turn off cursor
	//...
#endif
}

//=============================================================================
static int read_raw_key()
{
#ifndef WIN32
	//non-blocking poll / read key
	//http://stackoverflow.com/questions/3711830/set-a-timeout-for-reading-stdin
	fd_set selectset;
	struct timeval timeout = {0,100000}; //timeout seconds, microseconds
	int ret;
	FD_ZERO(&selectset);
	FD_SET(0,&selectset);
	ret =  select(1,&selectset,NULL,NULL,&timeout);
	if(ret == 0)
	{
		//timeout
		return 0;
	}
	else if(ret == -1)
	{
		//error
		return 0;
	}
//else
	// stdin has data, read it
	// (we know stdin is readable, since we only asked for read events
	//and stdin is the only fd in our select set.

	int count = read( STDIN_FILENO, (void*)keycodes, MAGIC_MAX_CHARS );

	return (count == 1)
		? keycodes[ 0 ]
		: -(int)(keycodes[ count -1 ]);
#else
	//https://msdn.microsoft.com/en-us/library/windows/desktop/ms687032%28v=vs.85%29.aspx
	//get a single key PRESS

	if (WaitForSingleObject(w_term_hstdin, 100) != WAIT_OBJECT_0)
	{
		return 0;
	}
/*
	do ReadConsoleInput( w_term_hstdin, &w_term_inrec, 1, &w_term_count );
	while ((w_term_inrec.EventType != KEY_EVENT) || !w_term_inrec.Event.KeyEvent.bKeyDown);
*/

	ReadConsoleInput( w_term_hstdin, &w_term_inrec, 1, &w_term_count );
	if((w_term_inrec.EventType != KEY_EVENT) || !w_term_inrec.Event.KeyEvent.bKeyDown)
	{
		return 0;
	}

	//restore the console to its previous state
	SetConsoleMode( w_term_hstdin, w_term_mode );

	return w_term_inrec.Event.KeyEvent.wVirtualKeyCode;
#endif
}//end read_raw_key()

//=============================================================================
static void reset_terminal()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//reset terminal to original settings
	tcsetattr( STDIN_FILENO, TCSANOW, &initial_settings );

	//turn on cursor
	fprintf(stderr,"%s",turn_on_cursor_seq);
#endif
}

//=============================================================================
static void init_term_seq()
{
#ifndef WIN32
	clear_to_eol_seq=	"\033[0J";
	turn_off_cursor_seq=	"\033[?25l";
	turn_on_cursor_seq=	"\033[?25h";
#else
	clear_to_eol_seq=	"                                                                     ";
	turn_on_cursor_seq=	"";
	turn_on_cursor_seq=	"";
#endif
}

//=============================================================================
static void init_key_codes()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	KEY_SPACE=32;
	KEY_Q=113;
	KEY_H=104;
	KEY_F1=-80;
	KEY_ARROW_LEFT=-68;
	KEY_ARROW_RIGHT=-67;
	KEY_ARROW_UP=-65;
	KEY_ARROW_DOWN=-66;
	KEY_HOME=-72;
	KEY_END=-70;
	KEY_BACKSPACE=127;
	KEY_M=109;
	KEY_L=108;
	KEY_COMMA=44;
	KEY_PERIOD=46;
	KEY_DASH=45;
#else
	KEY_SPACE=32;
	KEY_Q=81;
	KEY_H=72;
	KEY_F1=112;
	KEY_ARROW_LEFT=37;
	KEY_ARROW_RIGHT=39;
	KEY_ARROW_UP=38;
	KEY_ARROW_DOWN=40;
	KEY_HOME=36;
	KEY_END=35;
	KEY_BACKSPACE=8;
	KEY_M=77;
	KEY_L=76;
	KEY_COMMA=188;
	KEY_PERIOD=190;
	KEY_DASH=189;
#endif
}
//EOF
