#include "jack_playfile.h"

//tb/150612+

//simple file player for JACK
//inspired by jack_play, libsndfile, zresampler


//command line arguments
//========================================
const char *server_name = NULL;
const char *client_name = NULL;

static const char *filename=NULL; //mandatory

//start from absolute frame pos (skip n frames from start)
uint64_t frame_offset=0; //optional

//number of frames to read & play from offset (if argument not provided or 0: all frames)
uint64_t frame_count=0; //optional, only in combination with offset

//if set to 0, keyboard entry won't be used (except ctrl+c)
int keyboard_control_enabled=1;

//if set to 0, will not resample, even if file has different SR from JACK
int use_resampling=1;

//if set to 1, connect available file channels to available physical outputs
int autoconnect_jack_ports=1;

int try_jack_reconnect=1;

//debug: connect to jalv.gtk http://gareus.org/oss/lv2/sisco#Stereo_gtk
int connect_to_sisco=0;

//don't quit program when everything has played out
//static int pause_when_finished=0;

//toggle play/pause with 'space'
//if set to 0: prepare everything for playing but wait for user to toggle to play
int is_playing=1;

//toggle mute with 'm'
int is_muted=0;

//toggle loop with 'l'
int loop_enabled=0;

//0: frames, 1: seconds
int is_time_seconds=1;

//0: relative to frame_offset and frame_offset + frame_count
//1: relative to frame 0
int is_time_absolute=0;

//0: time remaining (-), 1: time elapsed
int is_time_elapsed=1;

//0: no running clock
int is_clock_displayed=1;

//========================================

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
//http://jack-audio.10948.n7.nabble.com/jack-options-t-td3483.html
static jack_options_t jack_opts = jack_options_t(JackNoStartServer | JackServerName);

static int jack_server_down=1;

//process() will return immediately if 0
static int process_enabled=0;
static int shutdown_in_progress=0;
static int shutdown_in_progress_signalled=0;

//disk thread
static pthread_t disk_thread={0};
static pthread_mutex_t disk_thread_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ok_to_read=PTHREAD_COND_INITIALIZER;
static int disk_thread_initialized=0;
static int disk_thread_finished=0;

//readers read into this buffer
static float* frames_from_file_buffer;

//handle to currently playing file
static SNDFILE *soundfile=NULL;

//holding basic audio file properties
static SF_INFO sf_info_sndfile;

//unified with other formats
static SF_INFO_GENERIC sf_info_generic;

//if sndfile can't open, try with mpg123
static mpg123_handle *soundfile_123=NULL;

//if found to be mp3 file
int is_mpg123=0;

//yet another reader
OggOpusFile  *soundfile_opus;

//if found to be opus file
int is_opus=0;

//..and another (for faster seeking)
OggVorbis_File soundfile_vorbis;

//if found to be opus file
int is_ogg_=0;

//multichannel float buffer for ov_read_float
float **ogg_buffer;

int is_flac_=0;

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

//arrows left and right, home, end
static int seek_frames_in_progress=0;

//relative seek, how many (native) frames
uint64_t seek_frames_per_hit=0;

//relative seek, how many seconds
double seek_seconds_per_hit=0;

//how many frames to seek
//calculated / limited in seek_frames and seek_frames_absolute
//executed in disk_thread
static uint64_t frames_to_seek=0;
static uint64_t frames_to_seek_type=SEEK_CUR; //or SEEK_SET

//10^0=1 - 10^8=10000000
int scale_exponent_frames=0;
int scale_exponent_frames_min=0;
int scale_exponent_frames_max=8;

//10-3=0.001 - 10^1=10, 2: 60 3: 600 4: 3600
int scale_exponent_seconds=1;
int scale_exponent_seconds_min=-3;
int scale_exponent_seconds_max=4;

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
static int KEY_C=0;

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
}

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

		opt=getopt_long(argc, argv, "", long_options, &option_index);

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

			case 'a':
				print_header();
				print_main_help();
				break;

			case 'b':
				print_version();
				exit(0);
				//break;

			case 'c':
				client_name=optarg;
				break;

			case 'd':
				server_name=optarg;
				break;

			case 'e':
				frame_offset=strtoull(optarg, NULL, 10);
				break;

			case 'f':
				frame_count=strtoull(optarg, NULL, 10);
				break;

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

	filename=argv[optind];

	memset (&sf_info_sndfile, 0, sizeof (sf_info_sndfile)) ;
	memset (&sf_info_generic, 0, sizeof (sf_info_generic)) ;
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
	soundfile=sf_open(filename,SFM_READ,&sf_info_sndfile);
	if(soundfile!=NULL)
	{
		sf_info_generic.frames=sf_info_sndfile.frames;
		sf_info_generic.samplerate=sf_info_sndfile.samplerate;
		sf_info_generic.channels=sf_info_sndfile.channels;
		sf_info_generic.format=sf_info_sndfile.format;

		//use vorbisfile to decode ogg
		if(is_ogg(sf_info_generic))
		{
			sf_close(soundfile);

			FILE *file_;
			file_  = fopen(filename,"r");
			ov_open(file_,&soundfile_vorbis,NULL,0);
			is_ogg_=1;
		}
	}
	else
	{
		//try opus

		int ret;
		soundfile_opus=op_open_file(filename,&ret);

		if(soundfile_opus!=NULL)
		{
			is_opus=1;

			//seek to end, get frame count
			sf_info_generic.frames=op_pcm_total(soundfile_opus,-1);
			sf_info_generic.samplerate=48000; ///
			sf_info_generic.channels=2; ///
			sf_info_generic.format=SF_FORMAT_OPUS | SF_FORMAT_FLOAT;
		}
		else
		{
			//try mp3
			mpg123_init();
			soundfile_123=mpg123_new(NULL, NULL);

			mpg123_format_none(soundfile_123);
			int format=mpg123_format(soundfile_123
				,48000
				,MPG123_STEREO
				,MPG123_ENC_FLOAT_32
			);

			int ret = mpg123_open(soundfile_123, filename);
			if(ret == MPG123_OK)
			{
				long rate;
				int channels, format;
				mpg123_getformat(soundfile_123, &rate, &channels, &format);

				struct mpg123_frameinfo mp3info;
				mpg123_info(soundfile_123, &mp3info);

				if(format==0)
				{
					fprintf (stderr, "/!\\ cannot open file \"%s\"\n", filename);
					exit(1);
				}

/*
				fprintf(stderr, "mp3 sampling rate %lu bitrate %d channels %d format %d\n"
					,rate
					,mp3info.bitrate
					,channels
					,format);
*/

				is_mpg123=1;

				//seek to end, get frame count
				sf_info_generic.frames=mpg123_seek(soundfile_123,0,SEEK_END);
				sf_info_generic.samplerate=48000; ///
				sf_info_generic.channels=2; ///
				sf_info_generic.format=SF_FORMAT_MP3 | SF_FORMAT_FLOAT;
			}
			else 
			{
				fprintf (stderr, "/!\\ cannot open file \"%s\"\n", filename);
				exit(1);
			}
		}//end try mpg123
	}//end try opus

	struct stat st;
	stat(filename, &st);
	file_size_bytes = st.st_size;

	fprintf(stderr,"file:        %s\n",filename);
	fprintf(stderr,"size:        %"PRId64" bytes (%.2f MB)\n",file_size_bytes,(float)file_size_bytes/1000000);

	is_flac_=is_flac(sf_info_generic);

	//flac has different seek behaviour than wav or ogg (SEEK_END (+0) -> -1)
	if((is_opus || is_flac(sf_info_generic)))
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

	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?).\nthis is fatal: jack_playfile needs JACK to run.\n");
		fprintf(stderr,"see http://jackaudio.org for more information on the JACK Audio Connection Kit.\n");
		exit(1);
	}

	const char **ports;
	jack_status_t status;

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
		client = jack_client_open (client_name, jack_opts, &status, server_name);

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

	//sampling rate ratio output (JACK) to input (file)
	out_to_in_sr_ratio=(double)jack_sample_rate/sf_info_generic.samplerate;

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
	int rb_interleaved_size_bytes		=50 * sndfile_request_frames    * output_port_count * bytes_per_sample;
	int rb_resampled_interleaved_size_bytes	=50 * jack_period_frames        * output_port_count * bytes_per_sample;
	int rb_deinterleaved_size_bytes		=50 * jack_period_frames        * output_port_count * bytes_per_sample;

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
static sf_count_t sf_seek_(sf_count_t offset, int whence)
{
	if(is_mpg123)
	{
		if(whence==SEEK_CUR && offset==0)
		{
			return mpg123_tell(soundfile_123);
		}
		else
		{
			return mpg123_seek(soundfile_123,offset,whence);
		}
	}
	else if(is_opus)
	{
		if(whence==SEEK_SET)
		{
			return op_pcm_seek(soundfile_opus,offset);
		}
		else if(whence==SEEK_CUR)
		{
			ogg_int64_t pos=op_pcm_tell(soundfile_opus);

			if(offset==0)
			{
				//no need to seek
				return pos;
			}
			else
			{
				//make relative seek absolute
				pos+=offset;
				return op_pcm_seek(soundfile_opus,pos);
			}
		}
	}
	else if(is_ogg_)
	{
		if(whence==SEEK_SET)
		{
			return ov_pcm_seek(&soundfile_vorbis,offset);
		}
		else if(whence==SEEK_CUR)
		{
			ogg_int64_t pos=ov_pcm_tell(&soundfile_vorbis);

			if(offset==0)
			{
				//no need to seek
				return pos;
			}
			else
			{
				//make relative seek absolute
				pos+=offset;
				return ov_pcm_seek(&soundfile_vorbis,pos);
			}
		}
	}
	else //sndfile
	{
		return sf_seek(soundfile,offset,whence);
	}
}

//=============================================================================
static void sf_close_()
{
	if(is_mpg123)
	{
		if(soundfile_123!=NULL)
		{
			mpg123_close(soundfile_123);
		}
	}
	else if(is_opus)
	{
		if(soundfile_opus!=NULL)
		{
			op_free(soundfile_opus);
		}
	}
	else if(is_ogg_)
	{
//		if(soundfile_vorbis!=NULL)
//		{
			ov_clear(&soundfile_vorbis);
//		}
	}
	else //sndfile
	{
		if(soundfile!=NULL)
		{
			sf_close(soundfile);
		}
	}
}

//=============================================================================
static void print_clock()
{
	if(!is_clock_displayed)
	{
		return;
	}

	sf_count_t pos=sf_seek_(0,SEEK_CUR);
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
			pos=sf_info_generic.frames-pos;
		}
	}

	seconds=frames_to_seconds(pos,sf_info_generic.samplerate);

	if(is_time_seconds)
	{
		if(seek_seconds_per_hit<1)
		{
			fprintf(stderr,"S %s %.3f %s %9.1f %s(%s) "
				,(is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(is_time_elapsed ? " " : "-")
				,frames_to_seconds(pos,sf_info_generic.samplerate)
				,(is_time_elapsed ? " " : "-")
				,format_duration_str(seconds));
		}
		else
		{
			fprintf(stderr,"S %s %5.0f  %s %9.1f %s(%s) "
				,(is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(is_time_elapsed ? " " : "-")
				,frames_to_seconds(pos,sf_info_generic.samplerate)
				,(is_time_elapsed ? " " : "-")
				,format_duration_str(seconds));
		}
	}
	else
	{
		//indicate high frame seek numbers witn k(ilo) and M(ega)
		uint64_t seek_fph=0;
		char *scale=" ";
		if(seek_frames_per_hit>1000000)
		{
			seek_fph=seek_frames_per_hit/1000000;
			scale="M";
		}
		else if(seek_frames_per_hit>1000)
		{
			seek_fph=seek_frames_per_hit/1000;
			scale="k";
		}
		else
		{
			seek_fph=seek_frames_per_hit;
		}

		fprintf(stderr,"F %s %5"PRId64"%s %s %9"PRId64" %s(%s) "
			,(is_time_absolute ? "abs" : "rel")
			,seek_fph
			,scale
			,(is_time_elapsed ? " " : "-")
			,pos
			,(is_time_elapsed ? " " : "-")
			,format_duration_str(seconds));
	}
}// end print_clock()

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

//		fprintf(stderr,"\nexp %d seek_frames_per_hit %"PRId64"\n",scale_exponent_frames,seek_frames_per_hit);
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
		fprintf(stderr,"^^ inc step");
		increment_seek_step_size();
	}
	//'v' (arrow down):
	else if(rawkey==KEY_ARROW_DOWN)
	{
		fprintf(stderr,"vv dec step");
		decrement_seek_step_size();
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
	//',':  toggle seconds/frames
	else if(rawkey==KEY_COMMA)
	{
		is_time_seconds=!is_time_seconds;
		fprintf(stderr,"time %s",is_time_seconds ? "seconds " : "frames ");

		if(is_time_seconds)
		{
			set_seconds_from_exponent();;
		}
		else
		{
			set_frames_from_exponent();
		}
	}
	//'.': toggle relative/absolute
	else if(rawkey==KEY_PERIOD)
	{
		is_time_absolute=!is_time_absolute;
		fprintf(stderr,"time %s",is_time_absolute ? "absolute " : "relative ");
	}
	//'-': toggle elapsed/remaining
	else if(rawkey==KEY_DASH)
	{
		is_time_elapsed=!is_time_elapsed;
		fprintf(stderr,"time %s",is_time_elapsed ? "elapsed " : "remaining ");
	}
	//'c': toggle clock on/off
	else if(rawkey==KEY_C)
	{
		is_clock_displayed=!is_clock_displayed;
		fprintf(stderr,"clock %s", is_clock_displayed ? "on " : "off ");
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
	fprintf(stderr,"\r%s\r\n",clear_to_eol_seq);
	fprintf(stderr,"HELP\n\n");

//	fprintf(stderr,"\r%s\rkeyboard shortcuts:\n",clear_to_eol_seq);
	fprintf(stderr,"keyboard shortcuts:\n\n");

	fprintf(stderr,"  h, f1:             help (this screen)\n");
	fprintf(stderr,"  space:             toggle play/pause\n");
	fprintf(stderr,"  < arrow left:      seek one step backward\n");
	fprintf(stderr,"  > arrow right:     seek one step forward\n");
	fprintf(stderr,"  ^ arrow up:        increment seek step size\n");
	fprintf(stderr,"  v arrow down:      decrement seek step size\n");
	fprintf(stderr,"  home, backspace:   seek to start\n");
	fprintf(stderr,"  end:               seek to end\n");
	fprintf(stderr,"  m:                 mute\n");
	fprintf(stderr,"  l:                 loop\n");
	fprintf(stderr,"  c:                 toggle clock display on / off\n");
	fprintf(stderr,"  , comma:           toggle clock seconds*  / frames\n");
	fprintf(stderr,"  . period:          toggle clock absolute* / relative\n");
	fprintf(stderr,"  - dash:            toggle clock elapsed*  / remaining\n");
	fprintf(stderr,"  q:                 quit\n\n");

	fprintf(stderr,"prompt:\n\n");
	fprintf(stderr,"|| paused   ML  S rel 0.001       943.1  (00:15:43.070)   \n");
	fprintf(stderr,"^           ^^  ^ ^   ^     ^     ^     ^ ^             ^ \n");
	fprintf(stderr,"1           23  4 5   6     7     8     7 9             10\n\n");
	fprintf(stderr,"  1): status playing '>', paused '||' or seeking '...'\n");
	fprintf(stderr,"  2): mute on/off 'M' or ' '\n");
	fprintf(stderr,"  3): loop on/off 'L' or ' '\n");
	fprintf(stderr,"  4): time and seek in seconds 'S' or frames 'F'\n");
	fprintf(stderr,"  5): time indication 'rel' to frame_offset or 'abs'\n");
	fprintf(stderr,"  6): seek step size in seconds or frames\n");
	fprintf(stderr,"  7): time elapsed ' ' or remaining '-'\n");
	fprintf(stderr,"  8): time in seconds or frames\n");
	fprintf(stderr,"  9): time in HMS.millis\n");
	fprintf(stderr," 10): keyboard input indication (i.e. seek)\n\n");

	//need command to print current props (file, offset etc)
}//end print_keyboard_shortcuts()

//=============================================================================
static void fill_jack_output_buffers_zero()
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
			req_buffer_from_disk_thread();
		}
	}

	resample();
	deinterleave();

	if(!is_playing || (seek_frames_in_progress && !loop_enabled && !all_frames_read))
	{
//		fprintf(stderr,".");
		fill_jack_output_buffers_zero();
		return 0;
	}

	if(all_frames_read
		&& jack_ringbuffer_read_space(rb_interleaved)==0
		&& jack_ringbuffer_read_space(rb_resampled_interleaved)==0
		&& jack_ringbuffer_read_space(rb_deinterleaved)==0)
	{
//		fprintf(stderr,"process(): diskthread finished and no more data in rb_interleaved, rb_resampled_interleaved, rb_deinterleaved\n");
//		fprintf(stderr,"process(): shutdown condition 1 met\n");

		fill_jack_output_buffers_zero();

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
//		fprintf(stderr,"\nprocess(): /!\\ ======== underrun\n");

		fill_jack_output_buffers_zero();
		print_stats();
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

	//seek in disk_thread
	frames_to_seek=seek;
	frames_to_seek_type=SEEK_SET;

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
	sf_count_t current_read_pos=sf_seek_(0,SEEK_CUR);

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

////need to reset more
}

//=============================================================================
static void setup_resampler()
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
			if (R.setup (sf_info_generic.samplerate, jack_sample_rate, sf_info_generic.channels, RESAMPLER_FILTERSIZE))
			{
				fprintf (stderr, "/!\\ sample rate ratio %d/%d is not supported.\n"
					,jack_sample_rate,sf_info_generic.samplerate);
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
					,sf_info_generic.samplerate
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

		resampling_finished=1;
	}
	else
	{
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

//==============================================================================
int64_t read_frames_from_file_to_buffer(uint64_t count, float *buffer)
{
	int64_t frames_to_go=count;
	int64_t could_read_frame_count=0;

	int file_eof=0;

	int ogg_buffer_offset=0;
	int current_section;

//	fprintf(stderr,"\nread_frames_from_file_to_buffer() called\n");

	while(frames_to_go>0 && !file_eof)
	{
//		fprintf(stderr,"\nto go %"PRId64"\n",frames_to_go);

		//==================================== sndfile
		if(!is_mpg123 && !is_opus && !is_ogg_)
		{
			could_read_frame_count=sf_readf_float(soundfile,(float*)buffer,frames_to_go);

			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);

				file_eof=1;
				break;
			}
			else
			{
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				buffer+=could_read_frame_count*output_port_count;

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);

				continue;
			}
		}//end sndfile
		//==================================== opus
		else if(is_opus)
		{
			could_read_frame_count=op_read_float_stereo(soundfile_opus,(float*)buffer
				,frames_to_go*2); //output_port_count

			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);

				file_eof=1;
				break;
			}
			else 
			{
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				buffer+=could_read_frame_count*output_port_count;

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);

				continue;
			}
		}//end opus
		//==================================== ogg
		else if(is_ogg_)
		{
			/*
			//https://xiph.org/vorbis/doc/vorbisfile/example.html
			//http://lists.xiph.org/pipermail/vorbis-dev/2002-January/005500.html

			long ov_read_float(OggVorbis_File *vf, float ***pcm_channels, int samples, int *bitstream);
			float **pcm; samples_read = ov_read_float(&vf, &pcm, 1024, &current_section)

			**pcm is a multichannel float vector. In stereo, for example, pcm[0] is left, and pcm[1] is right. 
			samples is the size of each channel.

			As you might expect, buffer[0][0] will be the first sample in the
			left channel, and buffer[1][0] will be the first sample in the right channel.

			Also make sure that you don't free() buffer anywhere since you don't own it.
			*/

			could_read_frame_count=ov_read_float(&soundfile_vorbis
				,&ogg_buffer
				,frames_to_go
				,&current_section);

			//interleave
			//(if no resampling is involved, we could directly put to rb_deinterleaved..)
			for(int i=0;i<could_read_frame_count;i++)
			{
				int buffer_index=i*output_port_count;

				for(int k=0;k<output_port_count;k++)
				{
					buffer[buffer_index+k+ogg_buffer_offset]=ogg_buffer[k][i];
//					fprintf(stderr,"buffer[%d] ogg_pcm[%d][%d]\n",buffer_index+k+ogg_buffer_offset,k,i);
					/*
					buffer: interleaved samples
					ogg_pcm [channel] [sample]
					...
					buffer[250] ogg_pcm[0][125]
					buffer[251] ogg_pcm[1][125]
					buffer[252] ogg_pcm[0][126]
					buffer[253] ogg_pcm[1][126]
					buffer[254] ogg_pcm[0][127]
					buffer[255] ogg_pcm[1][127]
					*/

				}
			}
			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);

				file_eof=1;
				break;
			}
			else
			{
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				ogg_buffer_offset+=could_read_frame_count*output_port_count;

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end ogg
		//==================================== mpg123
		else if(is_mpg123)
		{
			size_t fill=0;
			mpg123_read(soundfile_123,(unsigned char*)buffer
				,frames_to_go*output_port_count*bytes_per_sample, &fill) ;

			if(fill<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",fill);

				file_eof=1;
				break;
			}
			else
			{
				could_read_frame_count=fill/(output_port_count*bytes_per_sample);
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				buffer+=could_read_frame_count*output_port_count;

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);

				continue;
			}
		}//end mpg123
	}//end while(frames_to_go>0 && !file_eof)

	//in case of a normal, full buffer read, this will be equal to count
	return count-frames_to_go;
}//end read_frames_from_file_to_buffer

//=============================================================================
static int disk_read_frames()
{
//	fprintf(stderr,"disk_read_frames() called\n");

	if(total_frames_read_from_file>=frame_count)
	{
		if(loop_enabled)
		{
			total_frames_read_from_file=0;//frame_offset;
			seek_frames_in_progress=1;
			//sf_count_t new_pos=sf_seek(soundfile,frame_offset,SEEK_SET);
			sf_count_t new_pos=sf_seek_(frame_offset,SEEK_SET);
		}
		else
		{
			all_frames_read=1;
			return 0;
		}
	}

	uint64_t frames_to_go=frame_count-total_frames_read_from_file;
//	fprintf(stderr,"disk_read_frames(): frames to go %" PRId64 "\n",frames_to_go);

	//only read/write as many frames as requested (frame_count)
	int frames_read=(int)MIN(frames_to_go,sndfile_request_frames);

	if(frames_read==0)
	{
		return 0;
	}

	sf_count_t frames_read_from_file=0;

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

	if(frames_read_from_file>0)
	{
		disk_read_cycle_count++;
		total_bytes_read_from_file+=frames_read_from_file * output_port_count * bytes_per_sample_native;
		total_frames_read_from_file+=frames_read_from_file;
//		fprintf(stderr,"disk_read_frames(): frames: read %"PRId64" total %"PRId64"\n",frames_read_from_file,total_frames_read_from_file);

		if(total_frames_read_from_file>=frame_count)
		{
//			fprintf(stderr,"disk_read_frame(): all frames read from file\n");
			if(loop_enabled)
			{
#ifndef WIN32
				fprintf(stderr,"loop");
#endif
				total_frames_read_from_file=0;//frame_offset;

				seek_frames_in_progress=1;
				//sf_count_t new_pos=sf_seek(soundfile,frame_offset,SEEK_SET);
				sf_count_t new_pos=sf_seek_(frame_offset,SEEK_SET);

				return 1;
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
	//assume soundfile not null

	//seek to given offset position
	sf_count_t count=sf_seek_(frame_offset,SEEK_SET);

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
			sf_count_t count=sf_seek_(frames_to_seek,frames_to_seek_type);

			seek_frames_in_progress=0;
			frames_to_seek=0;

			sf_count_t new_pos=sf_seek_(0,SEEK_CUR);

			total_frames_read_from_file=new_pos-frame_offset;

//			fprintf(stderr,"\nseek end === new pos %"PRId64" total read %"PRId64"\n",new_pos,total_frames_read_from_file);
		}

		//don't read yet, possibly was started paused or another seek will follow shortly
		if(!is_playing)
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
		if(!disk_read_frames())//soundfile))
		{
			//disk_read() returns 0 on EOF
			goto done;
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

	sf_close_();
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

//=============================================================================
static void jack_shutdown_handler (void *arg)
{
//	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr, "\n/!\\ JACK server down!\n");

	jack_server_down=1;
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
	//else if ret>0
	//stdin has data, read it
	//(we know stdin is readable, since we only asked for read events
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
	//			 ---------------------------------------------------------------------
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
	KEY_C=99;
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
	KEY_C=67;///////
#endif
}
//EOF
