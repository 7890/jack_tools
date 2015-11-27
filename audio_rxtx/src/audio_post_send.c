/* audio_post_send -- receive uncompressed audio from a jack client as OSC
 * to further process and deliver outside the realtime jack environment
 * (this program is not a JACK client)
 *
 * Copyright (C) 2013 - 2014 Thomas Brand <tom@trellis.ch>
 *
 * This program is free software; feel free to redistribute it and/or 
 * modify it.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. bla.
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
//#include <jack/jack.h>
//#include <jack/ringbuffer.h>
//https://github.com/x42/weakjack#usage
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>

#include "jack_audio_common.h"
#include "weak_libjack.h"
#include "rb.h"

//tb/130427/131206//131211//131216/131229/150523
//gcc -o audio_post_send audio_post_send.c `pkg-config --cflags --libs liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

//between incoming UDP messages and outgoing TCP messages
rb_t *rb;

rb_t *rb_helper;

//will be updated according to blob count in messages
int input_port_count=2; //can't know yet

int output_port_count=2; //param
int port_count=2; //updated to minimum of in/out

//bytes, param MB
uint64_t max_buffer_size=0; //param

//defined by sender
uint64_t message_number=0;

//defined by "us", for tcp forwarding
uint64_t message_number_out=0;

//to detect gaps
uint64_t message_number_prev=0;

//count what we process
uint64_t process_cycle_counter=0;

//remote xruns since program start of sender
uint64_t remote_xrun_counter=0;

//buffer underflow for one period (all channels)
uint64_t multi_channel_drop_counter=0;

//limit messages from sender
//"signaling" not included
uint64_t receive_max=10000; //param

//misc measurements ins audio_handler()
//seconds
float time_interval=0;
double time_interval_sum=0;
float time_interval_avg=0;

//reset avg sum every 76. cycle (avoid "slow" comeback)
int avg_calc_interval=76;

//temporary counter (will be reset for avg calc)
uint64_t msg_received_counter=0;

//how many periods to drop (/buffer)
uint64_t requested_drop_count=0;

//to capture current time
struct timeval tv;
lo_timetag tt_prev;

//helper
int starting_transmission=0;

//osc
char sender_host[255];
char sender_port[10];

int remote_period_size=0;
int remote_sample_rate=0;

int close_on_incomp=0; //param

int channel_offset=0; //param

int lo_proto=LO_UDP;

char* remote_tcp_host; //param
char* remote_tcp_port; //param

//TCP
//2nd osc server
lo_server_thread lo_st_tcp;

//TCP remote (receiver) address
lo_address loa_tcp;

//ms
int delay_between_tcp_retries=700;
int delay_between_tcp_sends=10;

int last_lo_send_message_tcp_return=0;
uint64_t total_bytes_successfully_sent=0;

//ctrl+c etc
static void signal_handler(int sig)
{
	shutdown_in_progress=1;
	process_enabled=0;

	fprintf(stderr,"\nterminate signal %d received.\n",sig);

	if(close_on_incomp==0)
	{
		fprintf(stderr,"telling sender to pause.\n");

		//lo_address loa = lo_address_new(sender_host,sender_port);
		lo_address loa = lo_address_new_with_proto(lo_proto, sender_host,sender_port);

		lo_message msg=lo_message_new();
		lo_send_message (loa, "/pause", msg);
		lo_message_free(msg);
	}

	usleep(1000);

	fprintf(stderr,"cleaning up...");

	lo_server_thread_free(lo_st);
	rb_free(rb);
	rb_free(rb_helper);

	fprintf(stderr,"done.\n");
	
	fflush(stderr);

	exit(0);
}

void registerOSCMessagePatterns(const char *port);

void error(int num, const char *m, const char *path);

int audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
		void *data, void *user_data);

int offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
		void *data, void *user_data);

void print_info()
{
	uint64_t can_read_count=rb_can_read(rb);

	char* offset_string;
	if(channel_offset>0)
	{
		asprintf(&offset_string, "(%d+)", (channel_offset));
	}
	else
	{
		offset_string="";
	}

	char *warn_string="";
	if((float)can_read_count/max_buffer_size > 0.8)
	{
		warn_string="!";
	}

	if((message_number>0 && relaxed_display_counter>=update_display_every_nth_cycle*port_count)
		|| last_test_cycle==1
	)
	{
		fprintf(stderr,"\r# %" PRId64 " i: %s%d b: %.2f MB (%.2f%s) s: %.2f i: %.2f r: %" PRId64 
			/*" l: %" PRId64 */" d: %" PRId64 " o: %" PRId64 " p: %.1f # %" PRId64 ", %d (%d), %.2f MB%s",
			message_number,
			offset_string,
			input_port_count,
			(float)can_read_count/1000/1000,
			(float)can_read_count/max_buffer_size,
			warn_string,
			(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate,
			time_interval_avg*1000,
			remote_xrun_counter,/*local_xrun_counter,*/
			multi_channel_drop_counter,
			buffer_overflow_counter,
			(float)frames_since_cycle_start_avg/(float)period_size,
			message_number_out,last_lo_send_message_tcp_return,
			port_count,(float)total_bytes_successfully_sent/1000/1000,
			"\033[0J"
		);

		fflush(stderr);

		relaxed_display_counter=0;
	}
	relaxed_display_counter++;
}//end print_info

//this is not a JACK process cylce
int process()
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	if(process_enabled==1)
	{

		//as long as there is data ready to be sent, read and try to send
		//need: handle case where receiver can not read data fast enough
//		while(rb_can_read(rb)
//			>=input_port_count*bytes_per_sample*period_size)
//		{

		while(rb_can_read(rb)
			>=port_count*bytes_per_sample*period_size)
		{
			//fake consume
//			rb_advance_read_pointer(rb,port_count*bytes_per_sample*period_size);

			lo_message mm=lo_message_new();

			//add message counter
			lo_message_add_int64(mm,message_number_out);

			//indicate how many xruns (in sender's JACK)
			lo_message_add_int64(mm,remote_xrun_counter);

			gettimeofday(&tv, NULL);
			lo_timetag tt;
			tt.sec=(long)tv.tv_sec;
			tt.frac=(long)tv.tv_usec;
			lo_message_add_timetag(mm,tt);
			lo_message_add_int32(mm,sample_rate);


			//blob array, holding one period per channel
//			lo_blob blob[input_port_count];
			lo_blob blob[port_count];

			void* membuf = malloc(period_size*bytes_per_sample);

			int i;
			for( i=0; i<port_count; i++ )
			{
				//void* membuf = malloc(period_size*bytes_per_sample);
				rb_read (rb, (char*)membuf, period_size*bytes_per_sample);
				blob[i]=lo_blob_new(period_size*bytes_per_sample,membuf);
				lo_message_add_blob(mm,blob[i]);
			}
			int ret=lo_send_message(loa_tcp,"/audio",mm);
			last_lo_send_message_tcp_return=ret;

			if(ret<0)
			{
//				fprintf(stderr," TCP WARN: msg no: %" PRId64 " size: %" PRId64 " ret: %d ",message_number_out,lo_message_length(mm,"/audio"),ret);
			}
			else
			{
				total_bytes_successfully_sent+=ret;

//				fprintf(stderr," msg no: %" PRId64 " size: %" PRId64 " ret: %d ",message_number_out,lo_message_length(mm,"/audio"),ret);
				message_number_out++;
			}

			//free
			//lo_free(membuf);
			lo_message_free(mm);
			free(membuf);
			//free blobls ...
			for( i=0; i<port_count; i++ )
			{
				free(blob[i]);
			}

			if(ret<0)
			{
				return ret;
			}


		}//end while has data

		return 0;

	}//end if process enabled
	//process not yet enabled, buffering
	else
	{
		if(relaxed_display_counter>=update_display_every_nth_cycle
			|| last_test_cycle==1
		)
		{
			if((int)message_number<=0 && starting_transmission==0)
			{
				fprintf(stderr,"\rwaiting for audio input data...");
			}

			fflush(stderr);

			relaxed_display_counter=0;
		}
		relaxed_display_counter++;

	return 0;

	}//end if process not enabled

	//return 0;



} //end process


static void print_help (void)
{
	fprintf (stderr, "Usage: audio_post_send [Options] listening_port target_host target_port.\n");
	fprintf (stderr, "Options:          DUMMY !!!\n");
	fprintf (stderr, "  Display this text and quit         --help\n");
	fprintf (stderr, "  Show program version and quit      --version\n");
	fprintf (stderr, "  Show liblo properties and quit     --loinfo\n");
	fprintf (stderr, "  Number of channels to forward  (2) --out    <integer>\n");
	fprintf (stderr, "  Channel Offset                 (0) --offset <integer>\n");
	fprintf (stderr, "  Max buffer size (>= init) [MB](10) --max    <integer>\n");
	fprintf (stderr, "  Update info every nth cycle   (99) --update <integer>\n");
	fprintf (stderr, "  Limit processing count             --limit  <integer>\n");
	fprintf (stderr, "listening_port:   <integer>\n\n");
	fprintf (stderr, "target_host:      <string>\n\n");
	fprintf (stderr, "target_port:      <integer>\n\n");

	fprintf (stderr, "Example: jack_audio_receive --out 8 --connect --pre 200 1234\n");
	fprintf (stderr, "One message corresponds to one multi-channel (mc) period.\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools\n\n");
	exit (0);
}
int
main (int argc, char *argv[])
{

//////////////////////////////////////////////
//will be removed
//	sample_rate=44100;
	sample_rate=48000;
//	period_size=2048;
	period_size=4096;
//	period_size=256;
	//period_size=128;
	bytes_per_sample=4;

	//osc
	const char *listenPort;

	//command line options parsing
	//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
	static struct option long_options[] =
	{
		{"help",	no_argument,		0, 'h'},
		{"version",     no_argument,            0, 'v'},
		{"loinfo",      no_argument,            0, 'x'},
		{"out",		required_argument, 	0, 'o'},
		{"offset",	required_argument, 	0, 'f'},
		{"16",          no_argument,            0, 'y'},
		{"max",		required_argument,	0, 'm'},//max (allocate) buffer
		{"update",	required_argument,	0, 'u'},//screen info update every nth cycle
		{"limit",	required_argument,	0, 'l'},//test, stop after n processed
		{0, 0, 0, 0}
	};

	//print program header
	if(argc>1 && strcmp(argv[1],"--version"))
	{
		print_header("audio_post_send");
	}

	if (argc - optind < 1)
	{
		fprintf (stderr, "Missing arguments, see --help.\n\n");
		exit(1);
	}

	int opt;
 	//do until command line options parsed
	while (1)
	{
		/* getopt_long stores the option index here. */
		int option_index = 0;

		opt = getopt_long (argc, argv, "", long_options, &option_index);

		/* Detect the end of the options. */
		if (opt == -1)
		{
			break;
		}
		switch (opt)
		{
			case 0:

			 /* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0)
			{
				break;
			}

			case 'h':
				print_help();
				break;

			case 'v':
				print_version();
				break;

			case 'x':
				check_lo_props(1);
				return 1;

			case 'o':
				output_port_count=atoi(optarg);

				if(output_port_count>max_channel_count)
				{
					fprintf(stderr,"*** limiting playback ports to %d, sry\n",max_channel_count);
					output_port_count=max_channel_count;
				}
				port_count=fmin(input_port_count,output_port_count);
				break;

			case 'f':
				channel_offset=atoi(optarg);
				break;

			case 'm':
				//min 1 MB
				max_buffer_size=fmax(1,(uint64_t)atoll(optarg)*1000*1000);
				break;

			case 'u':
				update_display_every_nth_cycle=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'l':
				receive_max=fmax(1,(uint64_t)atoll(optarg));
				test_mode=1;
				fprintf(stderr,"*** limiting number of messages: %" PRId64 "\n",receive_max);

				break;

			case '?': //invalid commands
				/* getopt_long already printed an error message. */
				fprintf (stderr, "Wrong arguments, see --help.\n\n");
				exit(1);

				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameters listening port, remote host, remote port
	if(argc-optind != 3)
	{
		fprintf (stderr, "Wrong arguments, see --help.\n\n");
		exit(1);
	}

	if(check_lo_props(0)>0)
	{
		return 1;
	}

	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?). this is fatal: audio_post_send needs JACK to run.\n");
		//io_quit("nolibjack");
		exit(1);
	}

	listenPort=argv[optind];

	//tcp target
	remote_tcp_host=argv[optind+1];
	remote_tcp_port=argv[optind+2];

	loa_tcp = lo_address_new_with_proto(LO_TCP, remote_tcp_host, remote_tcp_port);

	//initialize time
	gettimeofday(&tv, NULL);
	tt_prev.sec=tv.tv_sec;
	tt_prev.frac=tv.tv_usec;

	//print startup info

	fprintf(stderr,"listening on UDP port: %s\n",listenPort);
	//udp/tcp use the same port for now
	fprintf(stderr,"started TCP server on port: %s\n",listenPort);

	fprintf(stderr,"channels (forward): %d\n",output_port_count);
	fprintf(stderr,"channel offset: %d\n",channel_offset);

	fprintf(stderr, "TCP target: %s:%s\n",remote_tcp_host,remote_tcp_port);

	fprintf(stderr, "period size (TCP forward): %d samples\n",period_size);

	fprintf(stderr, "delay between TCP sends: %d ms\n",delay_between_tcp_sends);
	fprintf(stderr, "delay between TCP retries on broken connection: %d ms\n",delay_between_tcp_retries);

	//ringbuffer size bytes
	uint64_t rb_size;

	//use as given via param --max or:
	if(max_buffer_size==0)
	{
		//default
		//10 MB           .  .  
		max_buffer_size=10000000;
	}

	//
	rb_size=max_buffer_size;

	fprintf(stderr,"allocated buffer size: %" PRId64 " bytes (%.2f MB)\n",max_buffer_size,(float)max_buffer_size/1000/1000);

	//====================================
	//main ringbuffer osc blobs -> jack output
	rb = rb_new (rb_size);
	//helper ringbuffer: used when remote period size < local period size
	rb_helper = rb_new (rb_size);

	if(rb==NULL)
	{
		fprintf(stderr,"could not create a ringbuffer with that size.\n");
		fprintf(stderr,"try --max <smaller size>.\n");
		exit(1);
	}

	/* install a signal handler to properly quits jack client */
#ifndef _WIN
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
#endif
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	//add osc hooks & start UDP server
	registerOSCMessagePatterns(listenPort);
	lo_server_thread_start(lo_st);

	//start TCP server, for forwarding to final receiver
	lo_st_tcp = lo_server_thread_new_with_proto(listenPort, LO_TCP, error);
	lo_server_thread_start(lo_st_tcp);

	fflush(stderr);

	/* keep running until the Ctrl+C */
	while(1) 
	{
		//possibly clean shutdown without any glitches
		if(shutdown_in_progress==1)
		{
			signal_handler(42);
		}

		//if tcp message could not be sent
		if(process()<0)
		{
			//wait x and update info
			int i;
			for(i=0;i<delay_between_tcp_retries;i++)
			{
				usleep(1000);
				print_info();
			}
		}
		else
		{
			//wait y and update info
			int i;
			for(i=0;i<delay_between_tcp_sends;i++)
			{
				usleep(1000);
				print_info();
			}
		}
	}
	exit (0);
}

void registerOSCMessagePatterns(const char *port)
{
	/* osc server */
	//lo_st = lo_server_thread_new(port, error);
	lo_st = lo_server_thread_new_with_proto(port, lo_proto, error);

/*
	/offer fiiiifh

	1) f: audio rx/tx format version
	2) i: sample rate
	3) i: bytes per sample
	4) i: period size
	5) i: channel count
	6) f: expected network data rate
	7) h: send / request counter
*/

	lo_server_thread_add_method(lo_st, "/offer", "fiiiifh", offer_handler, NULL);

/*
	/audio hhtib*

	1) h: message number
	2) h: xrun counter (sender side, as all the following meta data)
	3) t: timetag (seconds since Jan 1st 1900 in the UTC, fraction 1/2^32nds of a second)
	4) i: sampling rate
	5) b: blob of channel 1 (period size * bytes per sample) bytes long
	...
	...b: up to n channels
*/

	char typetag_string[1024];
	int v=0;
	for(v=0;v<1024;v++)
	{
		typetag_string[v]='\0';
	}

	//char *prefix="hhti";
	typetag_string[0]='h';
	typetag_string[1]='h';
	typetag_string[2]='t';
	typetag_string[3]='i';

	/////////////////
	int data_offset=4;

	v=0;
	for(v=0;v<max_channel_count;v++)
	{
		typetag_string[data_offset+v]='b';
		lo_server_thread_add_method(lo_st, "/audio", typetag_string, audio_handler, NULL);
	}
	//the max possible channel count depends on JACK period size, SR, liblo version (fixmax), 16/32 bit, 100/1000 mbit/s network

}//end registerocsmessages

//osc handlers
void error(int num, const char *msg, const char *path)
{
	if(shutdown_in_progress==0)
	{
		fprintf(stderr,"\r/!\\ liblo server error %d: %s %s", num, path, msg);
		//should be a param
	}
}

// /offer
//sender offers audio
int offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	float offered_format_version=argv[0]->f;

	int offered_sample_rate=argv[1]->i;
	int offered_bytes_per_sample=argv[2]->i;
	int offered_period_size=argv[3]->i;
	int offered_channel_count=argv[4]->i;

	float offered_data_rate=argv[5]->f;
	uint64_t request_counter=argv[6]->h;

	lo_message msg=lo_message_new();
	lo_address loa= lo_message_get_source(data);

	fprintf(stderr,"\nsender sample rate: %d\n",offered_sample_rate);
	fprintf(stderr,"sender bytes per sample: %d\n",offered_bytes_per_sample);

	//re-use for forwarding
	sample_rate=offered_sample_rate;
	bytes_per_sample=offered_bytes_per_sample;

	//check if compatible with sender
	//could check more stuff (channel count, data rate, sender host/port, ...)
	if(
		offered_sample_rate==sample_rate
		&& offered_bytes_per_sample==bytes_per_sample
		&& offered_format_version==format_version

		//new: support non-matching period sizes
		//&& offered_period_size==period_size
	)
	{
		remote_sample_rate=sample_rate;
		remote_period_size=offered_period_size;

		strcpy(sender_host,lo_address_get_hostname(loa));
		strcpy(sender_port,lo_address_get_port(loa));

		//sending accept will tell the sender to start transmission
		lo_send_message (loa, "/accept", msg);

		/*
		fprintf(stderr,"\nreceiving from %s:%s",
			lo_address_get_hostname(loa),lo_address_get_port(loa));
		*/

		starting_transmission=1;
	}


	//data is incompatible, handle depending on --close
	else if(close_on_incomp==0)
	{
		//sending deny will tell sender to stop/quit
		lo_message_add_float(msg,format_version);
		lo_message_add_int32(msg,sample_rate);
		lo_message_add_int32(msg,bytes_per_sample);
		lo_send_message (loa, "/deny", msg);

		fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings or format version on sender:\nformat version: %.2f\nSR: %d\nbytes per sample: %d\ntelling sender to stop.\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa),offered_format_version,offered_sample_rate,offered_bytes_per_sample
		);

		fflush(stderr);

		//shutting down is not a good strategy for the receiver in this case
		//shutdown_in_progress=1;
	}


	lo_message_free(msg);

	return 0;
} //end offer_handler


// /audio
//handler for audio messages
int audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	//init to 0, increment before use
	msg_received_counter++;

	gettimeofday(&tv, NULL);

	//first blob is at data_offset+1 (one-based)
	int data_offset=4;

	//ignore first n channels/blobs
	data_offset+=channel_offset;

	message_number_prev=message_number;

	//the messages are numbered sequentially. first msg is numberd 1
	message_number=argv[0]->h;

	if(message_number_prev<message_number-1)
	{
		fprintf(stderr,"\ngap in message sequence! possibly lost %" PRId64" message(s) on the way.\n"
			,message_number-message_number_prev-1);
		fflush(stderr);
	}

	//total args count minus metadata args and channel offset count = number of blobs
	input_port_count=argc-data_offset;

	//only process useful number of channels
	port_count=fmin(input_port_count,output_port_count);

	if(port_count < 1)
	{
		fprintf(stderr,"\n\nchannel offset %d >= available input channels %d! (nothing to receive). shutting down...\n"
			,channel_offset
			,(argc-data_offset+channel_offset));
		fflush(stderr);
		shutdown_in_progress=1;
		return 0;
	}

	//need to warn when offset + outchannels limited

	//check sample rate and period size if sender (re)started or values not yet initialized (=no /offer received)
	if(message_number_prev>message_number || message_number==1 || remote_sample_rate==0 || remote_period_size==0 )
	{
		lo_address loa;

		loa = lo_message_get_source(data);

		strcpy(sender_host,lo_address_get_hostname(loa));
		strcpy(sender_port,lo_address_get_port(loa));

		remote_sample_rate=argv[3]->i;

		remote_period_size=lo_blob_datasize((lo_blob)argv[0+data_offset])/bytes_per_sample;
		fprintf(stderr,"\nsender was (re)started. ");

		if(remote_period_size!=period_size)
		{
			fprintf(stderr,"sender period size: %d samples (%.3f x forward period size)\n\n",remote_period_size,(float)remote_period_size/period_size);
		}
		else
		{
			fprintf(stderr,"equal sender and receiver period size\n\n");
		}

	}//end if "no-offer init" was needed

	remote_xrun_counter=argv[1]->h;

	lo_timetag tt=argv[2]->t;

	double msg_time=tt.sec+(double)tt.frac/1000000;
	double msg_time_prev=tt_prev.sec+(double)tt_prev.frac/1000000;
	double time_now=tv.tv_sec+(double)tv.tv_usec/1000000;

	time_interval=msg_time-msg_time_prev;

	time_interval_sum+=time_interval;
	time_interval_avg=(float)time_interval_sum/msg_received_counter;

	tt_prev=tt;

	//reset avg calc, check and reset after use
	if(msg_received_counter>=avg_calc_interval)
	{
		msg_received_counter=0;
		time_interval_sum=0;
	}

	fflush(stderr);

	//
	process_enabled=1;

	int mc_period_bytes=period_size*bytes_per_sample*port_count;

	//check if a whole mc period can be written to the ringbuffer
	uint64_t can_write_count=rb_can_write(rb);
	if(can_write_count < mc_period_bytes)
	{
			buffer_overflow_counter++;
			/////////////////
			fprintf(stderr,"\nBUFFER OVERFLOW! this is bad -----%s\n","\033[0J");
			return 0;
	}

	//========================================
	//new: support different period sizes on sender / receiver (still need same SR)
	//this needs more tests and optimization
	if(period_size==remote_period_size)
	{
		int i;
		//don't read more channels than we have outputs
		for(i=0;i < port_count;i++)
		{
			//get blob (=one period of one channel)
			unsigned char *data = lo_blob_dataptr((lo_blob)argv[i+data_offset]);
			//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

			//write to ringbuffer
			//==========================================
			int cnt=rb_write(rb, (void *) data, 
				period_size*bytes_per_sample);
		}
	}
	else if(period_size>remote_period_size)
	{
		int i;
		//don't read more channels than we have outputs
		for(i=0;i < port_count;i++)
		{
			//get blob (=one period of one channel)
			unsigned char *data = lo_blob_dataptr((lo_blob)argv[i+data_offset]);
			//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

			//write to temporary ringbuffer until there is enough data
			//==========================================
			int cnt=rb_write(rb_helper, (void *) data, 
				remote_period_size*bytes_per_sample);
		}

		//if enough data collected for one larger multichannel period

		while(rb_can_read(rb_helper)	>=mc_period_bytes
		&& rb_can_write(rb)		>=mc_period_bytes)
		{
			//transfer from helper to main ringbuffer
			unsigned char* data;
			data=malloc(				mc_period_bytes);
			//store orig pointer
			unsigned char* orig_data=data;
			rb_read(rb_helper,data,	mc_period_bytes);

			for(i=0;i < port_count;i++)
			{
				int k;
				for(k=0;k<(period_size/remote_period_size);k++)
				{
					//reset pointer
					data=orig_data;
					//position in helper buffer for next sample for main buffer
					data+=	k*remote_period_size*bytes_per_sample*port_count
							+ i*remote_period_size*bytes_per_sample;

					//write one channel snipped (remote_period_size) to main buffer
					int w=rb_write(rb,(void *)data,remote_period_size*bytes_per_sample);
				}
			}
			data=orig_data;
			free(data);
		}
	}
	else if(period_size<remote_period_size)
	{
		int k;
		for(k=0;k<(remote_period_size/period_size);k++)
		{

			int i;
			//don't read more channels than we have outputs
			for(i=0;i < port_count;i++)
			{
				//get blob (=one period of one channel)
				unsigned char *data = lo_blob_dataptr((lo_blob)argv[i+data_offset]);
				//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

				//write to ringbuffer
				//==========================================
				data+=k*period_size*bytes_per_sample;

				int cnt=rb_write(rb, (void *) data, 
					period_size*bytes_per_sample);
			}
		}
	}

	return 0;
}//end audio_handler

