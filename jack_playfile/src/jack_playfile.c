#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <pthread.h>
#include <sndfile.h>

//#include <jack/jack.h>
//#include <jack/ringbuffer.h>
#include "weak_libjack.h"

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

typedef jack_default_audio_sample_t sample_t;

//tb/150612

//simple file player for JACK
//inspired by jack_play, libsndfile

//gcc -o jack_playfile jack_playfile.c`pkg-config --cflags --libs jack sndfile` -pthread

static float version=0.2;

//command line arguments
//======================
static const char *filename=NULL;

//start from absolute frame pos (skip n frames from start)
static long frame_offset=0;

//number of frames to read & play from offset (0: all)
static long frame_count=0;
//======================

//how many ports the jack client will have
//for now: will use the same count as file has channels
static int output_port_count=0;

//sndfile will read float, 4 bytes per sample
//good to use with jack buffers 
static int bytes_per_sample=4;//==sizeof(sample_t);

//set after connection to jack succeeded
static int jack_period_size=0;

//how many frames to read minimum via sndfile disk_read
static int sndfile_request_frames_minimum=256;

//can't be smaller than jack_period_size (frames), will be adjusted
static int sndfile_request_frames=0;

//counter for efectively read frames
//(may include more than requested frame_count because file is read in chunks)
static long total_frames_read_from_file=0;
//how many bytes to pad in last buffer (i.e. file EOF received or specific frame_count)
static long last_cycle_pad_frames=0;

//Array of pointers to jack input or output ports
static jack_port_t **ioPortArray;
static jack_client_t *client;
static jack_options_t jack_opts = JackNoStartServer;

//process() will return immediately if 0
static int process_enabled=0;
static int shutdown_in_progress=0;
static int shutdown_in_progress_signalled=0;

//small buffer between file reader and process()
static jack_ringbuffer_t *rb=NULL;

//counter for process() cycles where ringbuffer didn't have enough data for one cycle
//(one full period for every channel)
static unsigned long long underruns=0;

//Disk thread
static pthread_t disk_thread={0};
static pthread_mutex_t disk_thread_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_ready=PTHREAD_COND_INITIALIZER;
static int disk_thread_finished=0;

//handle to currently playing file
static SNDFILE *soundfile=NULL;

//================================================================
void signal_handler(int sig)
{
	fprintf(stderr,"total underruns: %d\n",underruns);
	if(sig!=42)
	{
		fprintf(stderr, "terminate signal %d received. cleaning up... ",sig);
	}
	process_enabled=0;

	jack_deactivate(client);
	//fprintf(stderr,"jack client deactivated. ");

	int index=0;
	while(ioPortArray[index]!=NULL && index<output_port_count)
	{
		jack_port_unregister(client,ioPortArray[index]);
		index++;
	}
	//fprintf(stderr,"jack ports unregistered. ");

	jack_client_close(client);
	fprintf(stderr,"jack client closed. ");

	if(rb!=NULL)
	{
		jack_ringbuffer_free(rb);
	}
	//fprintf(stderr,"ringbuffer freed. ");

	if(!disk_thread_finished)
	{
		// Close soundfile
		if(soundfile!=NULL)
		{
			sf_close (soundfile);
			//fprintf(stderr,"soundfile closed. ");
		}

		pthread_mutex_unlock (&disk_thread_lock);
		fprintf(stderr,"disk thread finished. ");
	}
	fprintf(stderr,"done\n");
	exit(0);
}

//================================================================
void jack_shutdown_handler (void *arg)
{
	fprintf(stderr, "jack server down!\n");
	exit(1);	
}

//================================================================
int process(jack_nframes_t nframes, void *arg) 
{
	if(!process_enabled
		|| shutdown_in_progress)
	{
		return 0;
	}

	//fprintf(stderr,".");

	if(jack_ringbuffer_read_space(rb) < output_port_count * sizeof(sample_t)*nframes)
	{
		underruns++;
		fprintf(stderr,"!");
		int i;
		for(i=0; i<output_port_count; i++)
		{
			if(!process_enabled)
			{
				return 0;
			}
			sample_t *o1;
			//get output buffer from jack for that channel
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);
			//set all samples zero
			memset(o1, 0, bytes_per_sample*nframes);
			/*int j=0;for(j=0;j<nframes;j++){o1[j]=0.3;}*/
		}

		if(disk_thread_finished)
		{
			shutdown_in_progress=1;
			return 0;
		}
	}
	else
	{
		int i;
		for(i=0; i<output_port_count; i++)
		{
			if(!process_enabled)
			{
				return 0;
			}
			sample_t *o1;			
			o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);
			//put samples from ringbuffer to jack output buffer
			jack_ringbuffer_read(rb, (char*)o1, bytes_per_sample*nframes);
		}
	}
	//===
	req_buffer_from_disk_thread();
	return 0;
}

//================================================================
int main(int argc, char *argv[])
{
	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	if( argc < 2	
		|| (argc >= 2 && 
			( strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	)
	{
		fprintf(stderr,"jack_playfile v%1.1f - (c) 2015 Thomas Brand <tom@trellis.ch>\n\n",version);
		fprintf(stderr,"syntax: jack_playfile <file> [frame offset [frame count]]\n\n");
		return(0);
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

	SF_INFO sf_info;
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

	// Init soundfile
	soundfile=sf_open(filename,SFM_READ,&sf_info);
	if(soundfile==NULL)
	{
		fprintf (stderr, "cannot open file \"%s\"\n(%s)\n", filename, sf_strerror(NULL));
		return 1;
	}
	sf_close (soundfile);

	//fprintf(stderr,"file:        %s\n",filename);
	print_file_info(sf_info);

	//for now: just create as many jack client output ports as the file has channels
	output_port_count=sf_info.channels;
	if(output_port_count<=0)
	{
		fprintf(stderr,"file has zero channels, nothing to play!\n");
		return 1;
	}

	//offset can't be negative or greater total frames in file
	if(frame_offset<0 || frame_offset>sf_info.frames)
	{
		frame_offset=0;
		fprintf(stderr,"frame_offset set to %d\n",frame_offset);
	}

	//if requested count negative, zero or greater total frames in file
	if(frame_count<=0 || frame_count>sf_info.frames)
	{
		//set possible max respecting frame_offset
		frame_count=sf_info.frames-frame_offset;
		fprintf(stderr,"frame_count set to %d",frame_count);
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

		fprintf(stderr,"frame_count set to %d\n",frame_count);
	}

	const char **ports;
	jack_status_t status;

	const char *client_name="jack_playfile";

	//create an array of output ports
	ioPortArray = (jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, jack_opts, &status, NULL);

	if (client == NULL) 
	{
		fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		exit(1);
	}

	if(sf_info.samplerate!=jack_get_sample_rate(client))
	{
		fprintf(stderr, "/!\\ file sample rate (%d Hz) different from JACK rate (%d Hz)\n"
			,sf_info.samplerate
			,jack_get_sample_rate(client));
	}

	jack_period_size=jack_get_buffer_size(client);

	jack_set_process_callback (client, process, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);

	// Register each output port
	int port=0;
	for (port=0 ; port<output_port_count ; port ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr, "Could not create portname for port %d\n", port);
			exit(1);
		}

		// Register the output port
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port] == NULL) 
		{
			fprintf(stderr, "Could not create output port %d\n", (port+1));
			exit(1);
		}
	}

	//now activate client in JACK, starting with process() cycles
	if (jack_activate (client)) 
	{
		fprintf (stderr, "cannot activate client\n\n");
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
		fprintf(stderr, "no physical capture ports found\n");
		exit(1);
	}
	
	int autoconnect=1;
	if(autoconnect)
	{
		int j=0;
		int i=0;
		for(i=0;i<output_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& ioPortArray[j]!=NULL 
				&& jack_port_name(ioPortArray[j])!=NULL)
			{
				if(!jack_connect (client, jack_port_name(ioPortArray[j]) , ports[i]))
				{
					//used variabled can't be NULL here
					fprintf (stderr, "autoconnect: %s -> %s\n",
						jack_port_name(ioPortArray[j]),ports[i]
					);
					j++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						jack_port_name(ioPortArray[j]),ports[i]
					);
				}
			}
		}
	}

	free (ports);

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	//take larger value
	sndfile_request_frames=MAX(sndfile_request_frames_minimum,jack_period_size);

	//===
	rb=jack_ringbuffer_create (50*sndfile_request_frames*output_port_count*bytes_per_sample);

	process_enabled=1;

	setup_disk_thread();

	//run possibly forever until not interrupted by any means
	while (1) 
	{
		//try clean shutdown, mainly to avoid possible audible glitches 
		if(shutdown_in_progress && !shutdown_in_progress_signalled)
		{
			shutdown_in_progress_signalled=1;
			signal_handler(42);
		}
		usleep(10000);
	}
	exit(0);
}//end main

//=============================================================================
static int disk_read(SNDFILE *soundfile,sample_t *buffer,size_t frames_requested)
{
	int file_nchannels=output_port_count;

	if(total_frames_read_from_file>=frame_count)
	{
		return 0;
	}

	sf_count_t frames_read_from_file;

	//===
	frames_read_from_file=sf_readf_float(soundfile,buffer,frames_requested);

	//now interleaved data in buffer
	if(frames_read_from_file!=0)
	{
		total_frames_read_from_file+=frames_read_from_file;
/*
		fprintf(stderr,"in disk_read: frames read %d total %d\n"
			,frames_read_from_file,total_frames_read_from_file);
*/
		// A partial read can occur at the end of the file.	Zero out
		// any part of the buffer not written by sf_readf_float().
		if(frames_read_from_file!=frames_requested)
		{
			last_cycle_pad_frames=frames_requested-frames_read_from_file;
			fprintf(stderr,"partial read at end of file %d %d\n"
				,frames_read_from_file, last_cycle_pad_frames);

			long read_end_diff=0;

			if(total_frames_read_from_file>frame_count)
			{
				read_end_diff=(total_frames_read_from_file-frame_count);

				///
				if(last_cycle_pad_frames+read_end_diff < jack_period_size)
				{
					last_cycle_pad_frames+=read_end_diff;
				}
				else
				{
					///
					fprintf(stderr,"bad!!\n");
				}
			}

			fprintf(stderr,"final padding %d %d\n",(frames_read_from_file-read_end_diff), last_cycle_pad_frames);

			bzero(buffer+(frames_read_from_file-read_end_diff)*file_nchannels
				,last_cycle_pad_frames*file_nchannels*sizeof(buffer[0]));
		}

		if(total_frames_read_from_file>frame_count 
			&& frames_read_from_file==frames_requested)
		{
			last_cycle_pad_frames=total_frames_read_from_file-frame_count;

			fprintf(stderr,"last cycle padding read/start/length: %d %d %d\n"
				,frames_read_from_file,last_cycle_pad_frames
				,(frames_read_from_file-last_cycle_pad_frames));

			bzero(buffer+(frames_read_from_file-last_cycle_pad_frames)*file_nchannels
				,last_cycle_pad_frames*file_nchannels*sizeof(buffer[0]));
		}
		return 1;
	}
	// If no frames were read assume we're at EOF
	return 0;
}

//=============================================================================
static void *disk_thread_func(void *arg)
{
	SNDFILE *soundfile;

	// Init soundfile
	SF_INFO sf_info;
	memset (&sf_info, 0, sizeof (sf_info));

	soundfile=sf_open(filename,SFM_READ,&sf_info);
	if(soundfile==NULL)
	{
		fprintf (stderr, "\ncannot open sndfile \"%s\" for input (%s)\n", filename,sf_strerror(NULL));
		jack_client_close(client);
		exit(1);
	}

	fprintf(stderr,"playing frames from/to/length: %d %d %d\n"
		,frame_offset
		/*,sf_info.frames*/
		,MIN(sf_info.frames,frame_offset+frame_count)
		,frame_count
	);

	//sf_count_t  sf_seek  (SNDFILE *sndfile, sf_count_t frames, int whence) ;
	sf_count_t count=sf_seek(soundfile,frame_offset,SEEK_SET);

	static int total_rb_write_cycles=0;

	void* data;
	data=malloc(sndfile_request_frames*output_port_count*bytes_per_sample);

	long bytepos_period=0;
	long iteration_counter=0;

	// Main disk loop
	for(;;)
	{
		//fprintf(stderr,"can write: %d\n",jack_ringbuffer_write_space(rb));
		if(jack_ringbuffer_write_space(rb) < sndfile_request_frames * output_port_count*bytes_per_sample )
		{
			//wait for buffer to be read out in process() to have write space again

			//===
			pthread_cond_wait (&data_ready, &disk_thread_lock);
			continue;
		}
		
		//===
		if(!disk_read(soundfile,data,sndfile_request_frames))
		{
			// disk_read() returns 0 on EOF
			goto done;
		}

		//deinterleave
/*
-in this example: all sizes in frames (not bytes)
-frame and channel counting starts at 1
-positions start at 0


i.e. 3 channels
read size: 8 (multichannel) frames

jack period size: 4 (multichannel) frames

8--------------------------------- read size
  ch3 f8  24       . ch3 f8  24
  ch2 f8  23       . ch3 f7  21
  ch1 f8  22       . ch3 f6  18
- ch3 f7  21       . ch3 f5  15
  ch2 f7  20       . ch2 f8  23
  ch1 f8  19       . ch2 f7  20
- ch3 f6  18       . ch2 f6  17
  ch2 f6  17       . ch2 f5  14
  ch1 f6  16       . ch1 f8  22
- ch3 f5  15       . ch1 f7  19
  ch2 f5  14       . ch1 f6  16
  ch1 f5  13       . ch1 f5  13
4 ---------------------------------- jack period size
  ch3 f4  12       . ch3 f4  12
  ch2 f4  11       . ch3 f3   9
  ch1 f4  10       . ch3 f2   6
- ch3 f3   9       . ch3 f1   3
  ch2 f3   8       . ch2 f4  11
  ch1 f3   7       . ch2 f3   8
2-ch3 f2   6       . ch2 f2   5  -- one channel period buffer
  ch2 f2   5       . ch2 f1   2
  ch1 f2   4       . ch1 f4  10
- ch3 f1   3       . ch1 f3   7
  ch2 f1   2       . ch1 f2   4
  ch1 f1   1       . ch1 f1   1 
0 --------------------------------- 

pseudo de-interleave code:

if enough write space in ringbuffer for read_size: (else continue)

read from file (8 multichannel frames)

loop 2    (read size / period size )   8 / 4
	period 1:
		loop 3   (channels)
			channel 1:
				loop 4   (period size)
					f1:	read  1, put to output pos  1, skip other channels
					f2:	read  4, put to output pos  2, skip other channels
					f3:	read  7, put to output pos  3, skip other channels
					f4:	read 10, put to output pos  4, skip other channels
			channel 2:
				loop 4   (period size)
					f1:	read  2, put to output pos  5, skip other channels
					f2:	read  5, put to output pos  6, skip other channels
					f3:	read  8, put to output pos  7, skip other channels
					f4:	read 11, put to output pos  8, skip other channels
			channel 3:
				loop 4   (period size)
					f1:	read  3, put to output pos  9, skip other channels
					f2:	read  6, put to output pos 10, skip other channels
					f3:	read  9, put to output pos 11, skip other channels
					f4:	read 12, put to output pos 12, skip other channels

---enough data for one ful multichannel jack period

	period 2:
		loop 3   (channels)
			channel 1:
				loop 4   (period size)
					f5:	read 13, put to output pos 13, skip other channels
					f6:	read 16, put to output pos 14, skip other channels
					f7:	read 19, put to output pos 15, skip other channels
					f8:	read 22, put to output pos 16, skip other channels
			channel 2:
				loop 4   (period size)
					f5:	read 14, put to output pos 17, skip other channels
					f6:	read 17, put to output pos 18, skip other channels
					f7:	read 20, put to output pos 19, skip other channels
					f8:	read 23, put to output pos 20, skip other channels
			channel 3:
				loop 4   (period size)
					f5:	read 15, put to output pos 21, skip other channels
					f6:	read 18, put to output pos 22, skip other channels
					f7:	read 21, put to output pos 23, skip other channels
					f8:	read 24, put to output pos 24, skip other channels

*/

		int period_loop=0;
		for(period_loop=0; period_loop < (sndfile_request_frames / jack_period_size ); period_loop++)
		{
			bytepos_period=period_loop * jack_period_size * output_port_count * bytes_per_sample;
			int bytepos_channel=0;

			int channel_loop=0;
			for(channel_loop=0; channel_loop < output_port_count; channel_loop++)
			{
				bytepos_channel=bytepos_period + channel_loop * bytes_per_sample;
				int bytepos_frame=0;

				int frame_loop=0;
				for(frame_loop=0; frame_loop < jack_period_size; frame_loop++)
				{
					bytepos_frame=bytepos_channel + frame_loop * output_port_count * bytes_per_sample;

/*
					fprintf(stderr,"iteration: %d loop #: period / channel / frame %d %d %d"
						,iteration_counter
						,period_loop
						,channel_loop
						,frame_loop
					);

					fprintf(stderr,"    bytepos: period / channel / frame / orig frame pos: %d %d %d %d\n"
						,bytepos_period
						,bytepos_channel
						,bytepos_frame
						,(bytepos_frame/bytes_per_sample)
					);
*/
					//read from disk_read buffer, at bytepos_frame, 1 sample
					float f1=*( (float*)(data + bytepos_frame) );

					//===
					//f1*=0.5;

					//put to ringbuffer
					jack_ringbuffer_write(rb,(float*)&f1,bytes_per_sample);

					iteration_counter++;
				}//frame
			}//channel

			total_rb_write_cycles+=1;
			//fprintf(stderr,"in disk_thread: rb write cycles: %d\n",total_rb_write_cycles);

		}//period

		//===
		pthread_cond_wait (&data_ready, &disk_thread_lock);
	}
done:

	// Close soundfile
	sf_close (soundfile);

	pthread_mutex_unlock (&disk_thread_lock);
	fprintf(stderr,"disk thread finished\n");

	disk_thread_finished=1;
	return 0;
}

//=============================================================================
void setup_disk_thread()
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&disk_thread_lock);
	pthread_create(&disk_thread, NULL, disk_thread_func, NULL);
}

//=============================================================================
void req_buffer_from_disk_thread()
{
	if(pthread_mutex_trylock (&disk_thread_lock)==0)
	{
		pthread_cond_signal (&data_ready);
		pthread_mutex_unlock (&disk_thread_lock);
	}
}

/*
//=============================================================================
static void stop_disk_thread(void)
{
 //Wake up disk thread. (no trylock) (isrunning==0)
	pthread_cond_signal(&data_ready);
	pthread_join(disk_thread, NULL);
}
*/

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char * format_duration_str(double seconds)
{
	static char str [128] ;
	int hrs, min ;
	double sec ;
	memset (str, 0, sizeof (str)) ;
	hrs = (int) (seconds / 3600.0) ;
	min = (int) ((seconds - (hrs * 3600.0)) / 60.0) ;
	sec = seconds - (hrs * 3600.0) - (min * 60.0) ;
	snprintf (str, sizeof (str) - 1, "%02d:%02d:%06.3f", hrs, min, sec) ;
	return str ;
}

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char * generate_duration_str(SF_INFO *sf_info)
{
	double seconds ;
	if (sf_info->samplerate < 1)
	return NULL ;
	if (sf_info->frames / sf_info->samplerate > 0x7FFFFFFF)
	return "unknown" ;
	seconds = (1.0 * sf_info->frames) / sf_info->samplerate ;
	/* Accumulate the total of all known file durations */
	//total_seconds += seconds ;
	return format_duration_str (seconds) ;
}

//=============================================================================
void print_file_info(SF_INFO sf_info)
{
	char* format_string;

	switch (sf_info.format & SF_FORMAT_TYPEMASK)
	{
	//http://www.mega-nerd.com/libsndfile/api.html
	/* Major formats. */
	case SF_FORMAT_WAV 	:format_string="Microsoft WAV format (little endian)"; break;
	case SF_FORMAT_AIFF 	:format_string="Apple/SGI AIFF format (big endian)"; break;
	case SF_FORMAT_AU 	:format_string="Sun/NeXT AU format (big endian)"; break;
	case SF_FORMAT_RAW 	:format_string="RAW PCM data"; break;
	case SF_FORMAT_PAF 	:format_string="Ensoniq PARIS file format"; break;
	case SF_FORMAT_SVX 	:format_string="Amiga IFF / SVX8 / SV16 format"; break;
	case SF_FORMAT_NIST 	:format_string="Sphere NIST format"; break;
	case SF_FORMAT_VOC 	:format_string="VOC files"; break;
	case SF_FORMAT_IRCAM 	:format_string="Berkeley/IRCAM/CARL"; break;
	case SF_FORMAT_W64 	:format_string="Sonic Foundry's 64 bit RIFF/WAV"; break;
	case SF_FORMAT_MAT4 	:format_string="Matlab (tm) V4.2 / GNU Octave 2.0"; break;
	case SF_FORMAT_MAT5  	:format_string="Matlab (tm) V5.0 / GNU Octave 2.1"; break;
	case SF_FORMAT_PVF 	:format_string="Portable Voice Format"; break;
	case SF_FORMAT_XI 	:format_string="Fasttracker 2 Extended Instrument"; break;
	case SF_FORMAT_HTK 	:format_string="HMM Tool Kit format"; break;
	case SF_FORMAT_SDS 	:format_string="Midi Sample Dump Standard"; break;
	case SF_FORMAT_AVR 	:format_string="Audio Visual Research"; break;
	case SF_FORMAT_WAVEX 	:format_string="MS WAVE with WAVEFORMATEX"; break;
	case SF_FORMAT_SD2 	:format_string="Sound Designer 2"; break;
	case SF_FORMAT_FLAC 	:format_string="FLAC lossless file format"; break;
	case SF_FORMAT_CAF 	:format_string="Core Audio File format"; break;
	case SF_FORMAT_WVE 	:format_string="Psion WVE format"; break;
	case SF_FORMAT_OGG 	:format_string="Xiph OGG container"; break;
	case SF_FORMAT_MPC2K 	:format_string="Akai MPC 2000 sampler"; break;
	case SF_FORMAT_RF64 	:format_string="RF64 WAV file"; break;
	default :
		format_string="unknown format!";
		break ;
	};

	char* sub_format_string;

	switch (sf_info.format & SF_FORMAT_SUBMASK)
	{
	/* Subtypes from here on. */
	case SF_FORMAT_PCM_S8       : sub_format_string="Signed 8 bit data"; break;
	case SF_FORMAT_PCM_16       : sub_format_string="Signed 16 bit data"; break;
	case SF_FORMAT_PCM_24       : sub_format_string="Signed 24 bit data"; break;
	case SF_FORMAT_PCM_32       : sub_format_string="Signed 32 bit data"; break;

	case SF_FORMAT_PCM_U8       : sub_format_string="Unsigned 8 bit data (WAV and RAW only)"; break;

	case SF_FORMAT_FLOAT        : sub_format_string="32 bit float data"; break;
	case SF_FORMAT_DOUBLE       : sub_format_string="64 bit float data"; break;

	case SF_FORMAT_ULAW         : sub_format_string="U-Law encoded"; break;
	case SF_FORMAT_ALAW         : sub_format_string="A-Law encoded"; break;
	case SF_FORMAT_IMA_ADPCM    : sub_format_string="IMA ADPCM"; break;
	case SF_FORMAT_MS_ADPCM     : sub_format_string="Microsoft ADPCM"; break;

	case SF_FORMAT_GSM610       : sub_format_string="GSM 6.10 encoding"; break;
	case SF_FORMAT_VOX_ADPCM    : sub_format_string="Oki Dialogic ADPCM encoding"; break;

	case SF_FORMAT_G721_32      : sub_format_string="32kbs G721 ADPCM encoding"; break;
	case SF_FORMAT_G723_24      : sub_format_string="24kbs G723 ADPCM encoding"; break;
	case SF_FORMAT_G723_40      : sub_format_string="40kbs G723 ADPCM encoding"; break;

	case SF_FORMAT_DWVW_12      : sub_format_string="12 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_16      : sub_format_string="16 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_24      : sub_format_string="24 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_N       : sub_format_string="N bit Delta Width Variable Word encoding"; break;

	case SF_FORMAT_DPCM_8       : sub_format_string="8 bit differential PCM (XI only)"; break;
	case SF_FORMAT_DPCM_16      : sub_format_string="16 bit differential PCM (XI only)"; break;

	case SF_FORMAT_VORBIS       : sub_format_string="Xiph Vorbis encoding"; break;
	default :
		sub_format_string="unknown subformat!";
		break ;
	};

	const char *duration_str;
	duration_str=generate_duration_str(&sf_info);

	fprintf(stderr,"format:      %s, %s (0x%08X)\nduration:    %s (%d frames)\nsamplerate:  %d\nchannels:    %d\n"
		,format_string, sub_format_string, sf_info.format
		,duration_str
		,sf_info.frames
		,sf_info.samplerate
		,sf_info.channels
	);
}//end print_file_info

//EOF
