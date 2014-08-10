/* jack_audio_receive -- receive uncompressed audio from another host via OSC
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
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>

#include "jack_audio_common.h"

//asprintf is a GNU extensions that is only declared when __GNU_SOURCE is set
#define __GNU_SOURCE

//tb/130427/131206//131211//131216/131229/150523
//gcc -o jack_audio_receiver jack_audio_receiver.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

//between incoming osc messages and jack process() callbacks
jack_ringbuffer_t *rb;

jack_ringbuffer_t *rb_helper;

//will be updated according to blob count in messages
int input_port_count=2; //can't know yet
int output_port_count=2; //param
int port_count=2; //updated to minimum of in/out

//fill n periods to buffer before playback
size_t pre_buffer_size=4; //param
size_t pre_buffer_counter=0;

//if no parameter given, buffer size
//is calculated later on
//periods
size_t max_buffer_size=0; //param

//defined by sender
uint64_t message_number=0;

//to detect gaps
uint64_t message_number_prev=0;

//count what we process
uint64_t process_cycle_counter=0;

//local xruns since program start of receiver
uint64_t local_xrun_counter=0;

//remote xruns since program start of sender
uint64_t remote_xrun_counter=0;

//buffer underflow for one period (all channels)
uint64_t multi_channel_drop_counter=0;

//if no data is available, fill with zero (silence)
//if set to 0, the current wavetable will be used
//if the network cable is plugged out, this can sound awful
int zero_on_underflow=1; //param

//if set to 0, /buffer ii messages are ignored
int allow_remote_buffer_control=1; //param

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
size_t requested_drop_count=0;

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

int rebuffer_on_restart=0; //param

int rebuffer_on_underflow=0; //param

int channel_offset=0; //param

int use_tcp=0; //param
int lo_proto=LO_UDP;

char* remote_tcp_server_port;

//ctrl+c etc
static void signal_handler(int sig)
{
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

	shutdown_in_progress=1;
	process_enabled=0;

	usleep(1000);

	fprintf(stderr,"cleaning up...");

	jack_client_close(client);
	lo_server_thread_free(lo_st);
	jack_ringbuffer_free(rb);
	jack_ringbuffer_free(rb_helper);

	fprintf(stderr,"done.\n");

	exit(0);
}

void registerOSCMessagePatterns(const char *port);

void error(int num, const char *m, const char *path);

int audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
		void *data, void *user_data);

int offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
		void *data, void *user_data);

int buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
		void *data, void *user_data);

//jack calls this method on every xrun
int xrun()
{
	local_xrun_counter++;
	return 0;
}

void print_info()
{
	size_t can_read_count=jack_ringbuffer_read_space(rb);

	char* offset_string;
	if(channel_offset>0)
	{
		asprintf(&offset_string, "(%d+)", (channel_offset));
	}
	else
	{
		offset_string="";
	}

	//this is per channel, not per cycle. *port_count
	if(relaxed_display_counter>=update_display_every_nth_cycle*port_count
		|| last_test_cycle==1
	)
	{
		fprintf(stderr,"\r# %" PRId64 " i: %s%d f: %.1f b: %zu s: %.4f i: %.2f r: %" PRId64 
			" l: %" PRId64 " d: %" PRId64 " o: %" PRId64 " p: %.1f%s",
			message_number,
			offset_string,
			input_port_count,
			(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count,
			can_read_count,
			(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate,
			time_interval_avg*1000,
			remote_xrun_counter,local_xrun_counter,
			multi_channel_drop_counter,
			buffer_overflow_counter,
			(float)frames_since_cycle_start_avg/(float)period_size,
			"\033[0J"
		);
		relaxed_display_counter=0;
	}
	relaxed_display_counter++;
}//end print_info

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int
process (jack_nframes_t nframes, void *arg)
{
	//if shutting down fill buffers with 0 and return
	if(shutdown_in_progress==1)
	{
		int i;
		for( i=0; i < output_port_count; i++ )
		{
			jack_default_audio_sample_t *o1;
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (ioPortArray[i], nframes);
			memset ( o1, 0, bytes_per_sample*nframes );
		}
		return 0;
	}

	//if no data for this cycle (all channels) 
	//is available (!), fill buffers with 0 or re-use old buffers and return
	if(jack_ringbuffer_read_space(rb) < port_count * bytes_per_sample*nframes
			&& process_enabled==1
	)
	{

		int i;
		for( i=0; i < output_port_count; i++ )
		{
			if(zero_on_underflow==1)
			{
				jack_default_audio_sample_t *o1;
				o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (ioPortArray[i], nframes);
				memset ( o1, 0, bytes_per_sample*nframes );
			}
			print_info();
		}

		multi_channel_drop_counter++;

		if(rebuffer_on_underflow==1)
		{
			pre_buffer_counter=0;
			process_enabled=0;
		}

		//reset avg calculation
		time_interval_avg=0;
		msg_received_counter=0;
		fscs_avg_counter=0;

		return 0;
	}

	if(process_enabled==1)
	{
		process_cycle_counter++;

		if(process_cycle_counter>receive_max-1 && test_mode==1)
		{
			last_test_cycle=1;
		}
	}

	//init to 0. increment before use
	fscs_avg_counter++;

	frames_since_cycle_start_sum+=frames_since_cycle_start;
	frames_since_cycle_start_avg=frames_since_cycle_start_sum/fscs_avg_counter;

	//check and reset after use
	if(fscs_avg_calc_interval>=fscs_avg_counter)
	{
		fscs_avg_counter=0;
		frames_since_cycle_start_sum=0;	
	}

	//if sender sends more channels than we have output channels, ignore them
	int i;
	for( i=0; i < port_count; i++ )
	{
		jack_default_audio_sample_t *o1;
		o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (ioPortArray[i], nframes);

		if(process_enabled==1)
		{
			jack_ringbuffer_read (rb, (char*)o1, bytes_per_sample*nframes);

			/*
			fprintf(stderr,"\rreceiving from %s:%s",
				sender_host,sender_port
			);
			*/

			print_info();

		} // end if process enabled
		//process not yet enabled, buffering
		else
		{
			//this is per channel, not per cycle. *port_count
			if(relaxed_display_counter>=update_display_every_nth_cycle*port_count
				|| last_test_cycle==1
			)
			{
				//only for init
				if((int)message_number<=0 && starting_transmission==0)
				{
					fprintf(stderr,"\rwaiting for audio input data...");
				}
				else
				{
					fprintf(stderr,"\r# %" PRId64 " buffering... mc periods to go: %zu%s",
						message_number,
						pre_buffer_size-pre_buffer_counter,
						"\033[0J"
					);
				}

				relaxed_display_counter=0;
			}
			relaxed_display_counter++;

			//set output buffer silent
			memset ( o1, 0, port_count*bytes_per_sample*nframes );
		}
	}

	//if sender sends less channels than we have output channels, wee need to fill them with 0
	if(input_port_count < output_port_count)
	{
		for(i=0;i < (output_port_count-input_port_count);i++)
		{
			jack_default_audio_sample_t *o1;
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (ioPortArray[input_port_count+i], nframes);
		}
	}

	if(process_enabled==1)
	{
		//requested via /buffer, for test purposes (make buffer "tight")
		if(requested_drop_count>0)
		{
			size_t drop_bytes_count=requested_drop_count
				*port_count*period_size*bytes_per_sample;

			jack_ringbuffer_read_advance(rb,drop_bytes_count);

			requested_drop_count=0;
			multi_channel_drop_counter=0;
		}
	}

	if(last_test_cycle==1)
	{
		fprintf(stderr,"\ntest finished after %" PRId64 " process cycles\n",process_cycle_counter);
		fprintf(stderr,"(waiting and buffering cycles not included)\n");
		shutdown_in_progress=1;
	}

	//simulate long cycle process duration
	//usleep(1000);

	frames_since_cycle_start=jack_frames_since_cycle_start(client);

	return 0;
} //end process()

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
	lo_server_thread_free(lo_st);
	exit (1);
}

static void print_help (void)
{
	fprintf (stderr, "Usage: jack_audio_receive [Options] listening_port.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                 --help\n");
	fprintf (stderr, "  Number of playback channels:   (2) --out    <integer>\n");
	fprintf (stderr, "  Channel Offset:                (0) --offset <integer>\n");
	fprintf (stderr, "  Autoconnect ports:           (off) --connect\n");
	fprintf (stderr, "  JACK client name:        (receive) --name   <string>\n");
	fprintf (stderr, "  JACK server name:        (default) --sname  <string>\n");
	fprintf (stderr, "  Initial buffer size:(4 mc periods) --pre    <integer>\n");
	fprintf (stderr, "  Max buffer size (>= init):  (auto) --max    <integer>\n");
	fprintf (stderr, "  Rebuffer on sender restart:  (off) --rere\n");
	fprintf (stderr, "  Rebuffer on underflow:       (off) --reuf\n");
	fprintf (stderr, "  Re-use old data on underflow: (no) --nozero\n");
	fprintf (stderr, "  Allow ext. buffer control    (yes) --norbc\n");
	fprintf (stderr, "  Update info every nth cycle   (99) --update <integer>\n");
	fprintf (stderr, "  Limit processing count:      (off) --limit  <integer>\n");
	fprintf (stderr, "  Quit on incompatibility:     (off) --close\n");
//	fprintf (stderr, "  Use TCP instead of UDP       (UDP) --tcp    <integer>\n");
	fprintf (stderr, "listening_port:   <integer>\n\n");
	fprintf (stderr, "Example: jack_audio_receive --out 8 --connect --pre 200 1234\n");
	fprintf (stderr, "One message corresponds to one multi-channel (mc) period.\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools\n\n");
	//needs manpage
	exit (1);
}
int
main (int argc, char *argv[])
{
	//jack
	const char **ports;
	const char *client_name="receive"; //param
	const char *server_name = NULL;
	//jack_options_t options = JackNullOption;
	jack_status_t status;

	//osc
	const char *listenPort;

	//command line options parsing
	//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
	static struct option long_options[] =
	{
		{"help",	no_argument,		0, 'h'},
		{"out",		required_argument, 	0, 'o'},
		{"offset",	required_argument, 	0, 'f'},
		{"connect",	no_argument,	&autoconnect, 1},
		{"name",	required_argument,	0, 'n'},
		{"sname",	required_argument,	0, 's'},
		{"pre",		required_argument,	0, 'b'},//pre (delay before playback) buffer
		{"max",		required_argument,	0, 'm'},//max (allocate) buffer
		{"rere",	no_argument,	&rebuffer_on_restart, 1},
		{"reuf",	no_argument,	&rebuffer_on_underflow, 1},
		{"nozero",	no_argument,	&zero_on_underflow, 0},
		{"norbc",	no_argument,	&allow_remote_buffer_control, 0},
		{"update",	required_argument,	0, 'u'},//screen info update every nth cycle
		{"limit",	required_argument,	0, 'l'},//test, stop after n processed
		{"close",	no_argument,	&close_on_incomp, 1},//close client rather than telling sender to stop
		{"tcp",		required_argument,	0, 't'}, //server port of remote host
		{0, 0, 0, 0}
	};

	//print program header
	print_header("jack_audio_receive");

	if (argc - optind < 1)
	{
		fprintf (stderr, "Missing arguments, try --help.\n\n");
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

			case 'n':
				client_name=optarg;
				break;

			case 's':
				server_name=optarg;
				jack_opts |= JackServerName;
				break;

			case 'b':
				pre_buffer_size=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'm':
				max_buffer_size=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'u':
				update_display_every_nth_cycle=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'l':
				receive_max=fmax(1,(uint64_t)atoll(optarg));
				test_mode=1;
				fprintf(stderr,"*** limiting number of messages: %" PRId64 "\n",receive_max);

				break;

			case 't':
				use_tcp=1;
				remote_tcp_server_port=optarg;
				break;

			case '?': //invalid commands
				/* getopt_long already printed an error message. */
				fprintf (stderr, "Wrong arguments, try --help.\n\n");
				exit(1);

				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameters listening port
	if(argc-optind != 1)
	{
		fprintf (stderr, "Wrong arguments, try --help.\n\n");
		exit(1);
	}

	listenPort=argv[optind];

	if(use_tcp==1)
	{
		lo_proto=LO_TCP;
	}

	//initialize time
	gettimeofday(&tv, NULL);
	tt_prev.sec=tv.tv_sec;
	tt_prev.frac=tv.tv_usec;

	//create an array of input ports
	ioPortArray = (jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	ioBufferArray = (jack_default_audio_sample_t**) malloc(output_port_count * sizeof(jack_default_audio_sample_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, jack_opts, &status, server_name);
	if (client == NULL) {
		fprintf(stderr,"jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf(stderr,"Unable to connect to JACK server.\n");
		}
		exit (1);
		}
	if (status & JackServerStarted) {
		fprintf(stderr,"JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf (stderr, "*** unique name `%s' assigned\n", client_name);
	}

	//print startup info

	fprintf(stderr,"listening on osc port: %s\n",listenPort);

	read_jack_properties();
	print_common_jack_properties();

	fprintf(stderr,"channels (playback): %d\n",output_port_count);
	fprintf(stderr,"channel offset: %d\n",channel_offset);

	fprintf(stderr,"multi-channel period size: %d bytes\n",
		output_port_count*period_size*bytes_per_sample
	);

	char *strat="fill with zero (silence)";
	if(zero_on_underflow==0)
	{
		strat="re-use last available period";
	}

	fprintf(stderr,"underflow strategy: %s\n",strat);

	if(rebuffer_on_restart==1)
	{
		fprintf(stderr,"rebuffer on sender restart: yes\n");
	}
	else
	{
		fprintf(stderr,"rebuffer on sender restart: no\n");
	}

	if(rebuffer_on_underflow==1)
	{
		fprintf(stderr,"rebuffer on underflow: yes\n");
	}
	else
	{
		fprintf(stderr,"rebuffer on underflow: no\n");
	}

	if(allow_remote_buffer_control==1)
	{
		fprintf(stderr,"allow external buffer control: yes\n");
	}
	else
	{
		fprintf(stderr,"allow external buffer control: no\n");
	}

	if(close_on_incomp==1)
	{
		fprintf(stderr,"shutdown receiver when incompatible data received: yes\n");
	}
	else
	{
		fprintf(stderr,"shutdown receiver when incompatible data received: no\n");
	}

/*
	if(use_tcp==1)
	{
		fprintf(stderr, "network transmission style: TCP\n");
	}
	else
	{
		fprintf(stderr, "network transmission style: UDP\n");
	}
*/

	char buf[64];
	format_seconds(buf,(float)pre_buffer_size*period_size/(float)sample_rate);

	size_t rb_size_pre=pre_buffer_size*output_port_count*period_size*bytes_per_sample;

	fprintf(stderr,"initial buffer size: %zu mc periods (%s, %zu bytes, %.2f mb)\n",
		pre_buffer_size,
		buf,
		rb_size_pre,
		(float)rb_size_pre/1000/1000
	);
	buf[0] = '\0';

	//ringbuffer size bytes
	size_t rb_size;

	//ringbuffer mc periods
	int max_buffer_mc_periods;

	//max given as param (user knows best. if pre=max, overflows are likely)
	if(max_buffer_size>0)
	{
		max_buffer_mc_periods=fmax(pre_buffer_size,max_buffer_size);
		rb_size= max_buffer_mc_periods
			*output_port_count*period_size*bytes_per_sample;
	}
	else //"auto"
	{
		//make max buffer 0.5 seconds larger than pre buffer
		max_buffer_mc_periods=pre_buffer_size+ceil(0.5*(float)sample_rate/period_size);
		rb_size=max_buffer_mc_periods
			*output_port_count*period_size*bytes_per_sample;
	}

	max_buffer_size=max_buffer_mc_periods;

	format_seconds(buf,(float)max_buffer_mc_periods*period_size/sample_rate);
	fprintf(stderr,"allocated buffer size: %zu mc periods (%s, %zu bytes, %.2f mb)\n",
		max_buffer_size,
		buf,
		rb_size,
		(float)rb_size/1000/1000
	);
	buf[0] = '\0';

	//====================================
	//main ringbuffer osc blobs -> jack output
	rb = jack_ringbuffer_create (rb_size);
	//helper ringbuffer: used when remote period size < local period size
	rb_helper = jack_ringbuffer_create (rb_size);

	if(rb==NULL)
	{
		fprintf(stderr,"could not create a ringbuffer with that size.\n");
		fprintf(stderr,"try --max <smaller size>.\n");
		exit(1);
	}

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/
	//NULL could be config/data struct
	jack_set_process_callback(client, process, NULL);

	jack_set_xrun_callback(client, xrun, NULL);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/
	jack_on_shutdown (client, jack_shutdown, 0);

	// Register each output port
	int port;
	for (port=0 ; port<output_port_count ; port ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr,"Could not create portname for port %d", port);
			exit(1);
		}

		// Register the output port
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port] == NULL) 
		{
			fprintf(stderr,"Could not create output port %d\n", (port+1));
			exit(1);
		}
	}

	/* Tell the JACK server that we are ready to roll. Our
	 * process() callback will start running now. */
	if (jack_activate (client)) 
	{
		fprintf (stderr, "cannot activate client");
		exit (1);
	}

	/* Connect the ports. You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running. Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */

	//prevent to get physical midi ports
	const char* pat="audio";

	ports = jack_get_ports (client, NULL, pat, JackPortIsPhysical|JackPortIsInput);

	if (ports == NULL) 
	{
		fprintf(stderr,"no physical playback ports\n");
		//exit (1);
	}
	
	if(autoconnect==1)
	{
		fprintf (stderr, "\n");

		int j=0;
		int i;
		for(i=0;i<output_port_count;i++)
		{
			if (ports[i]!=NULL)
			{
				if(!jack_connect (client, jack_port_name(ioPortArray[j]), ports[i]))
				{
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
		fprintf (stderr, "\n");
	}

	free (ports);

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	//add osc hooks & start server
	registerOSCMessagePatterns(listenPort);
	lo_server_thread_start(lo_st);

	/* keep running until the Ctrl+C */
	while (1) {
		//possibly clean shutdown without any glitches
		if(shutdown_in_progress==1)
		{
			signal_handler(42);
		}
		sleep (1);
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
	experimental

	/buffer ii

	1) i: target fill value for available periods in ringbuffer (multi channel)
	2) i: max. target buffer size in mc periods
	drop or add periods
	this will introduce a hearable click on drops
	or pause playback for rebuffering depending on the current buffer fill
*/

	lo_server_thread_add_method(lo_st, "/buffer", "ii", buffer_handler, NULL);

/*
	/audio hhtib*

	1) h: message number
	2) h: xrun counter (sender side, as all the following meta data)
	3) t: timetag (seconds since Jan 1st 1900 in the UTC, fraction 1/2^32nds of a second)
	4) i: sampling rate
	5) b: blob of channel 1 (period size * bytes per sample) bytes long
	...
	68) b: up to 64 channels
*/

	//support 1-64 blobs / channels per message
	lo_server_thread_add_method(lo_st, "/audio", "hhtib", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbb", audio_handler, NULL);
//8
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbb", audio_handler, NULL);
//16
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
//32

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
//64. ouff. maybe using a generic handler?
//this is a theoretical value, working on localhost at best
//to test 64 channels, use a small period size

}//end registerocsmessages

//osc handlers
void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d: %s\n", num, msg);
	exit(1);
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

	//send back to host that offered audio
	//lo_address loa = lo_message_get_source(data);

	lo_address loa;

	if(use_tcp==1)
	{
		lo_address loa_ = lo_message_get_source(data);
		loa = lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
	}
	else
	{
		loa = lo_message_get_source(data);
	}

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
		lo_send_message (loa, "/deny", msg);

		fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings or format version on sender:\nformat version: %.2f\nSR: %d\ntelling sender to stop.\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa),offered_format_version,offered_sample_rate
		);

		//shutting down is not a good strategy for the receiver in this case
		//shutdown_in_progress=1;
	}
	else
	{
		fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings or format version on sender\nformat version: %.2f\nSR: %d\nshutting down... (see option --close)\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa),offered_format_version,offered_sample_rate
		);

		shutdown_in_progress=1;
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
	}

	//total args count minus metadata args count = number of blobs
	input_port_count=argc-data_offset;

	//only process useful number of channels
	port_count=fmin(input_port_count,output_port_count);

	if(port_count < 1)
	{
		fprintf(stderr,"channel offset %d >= available input channels %d! (nothing to receive). shutting down...\n"
			,channel_offset
			,channel_offset+input_port_count);
		shutdown_in_progress=1;
		return 0;
	}

	//check sample rate and period size if sender (re)started or values not yet initialized (=no /offer received)
	if(message_number_prev>message_number || message_number==1 || remote_sample_rate==0 || remote_period_size==0 )
	{
		lo_address loa;

		if(use_tcp==1)
		{
			lo_address loa_ = lo_message_get_source(data);
			loa = lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
		}
		else
		{
			loa = lo_message_get_source(data);
		}

		strcpy(sender_host,lo_address_get_hostname(loa));
		strcpy(sender_port,lo_address_get_port(loa));

		//option --rebuff
		if(rebuffer_on_restart==1)
		{
			size_t can_read_count=jack_ringbuffer_read_space(rb);
			pre_buffer_counter=fmax(0,(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count);
			//start buffering
			process_enabled=0;
		}
		else
		{
			pre_buffer_counter=0;
		}

		remote_sample_rate=argv[3]->i;

		if(sample_rate!=remote_sample_rate)
		{
			if(close_on_incomp==0)
			{
				//sending deny will tell sender to stop/quit
				lo_message msg = lo_message_new();

				lo_message_add_float(msg,format_version);
				lo_message_add_int32(msg,sample_rate);
				lo_send_message (loa, "/deny", msg);
				lo_message_free(msg);

				fprintf(stderr,"\ndenying transmission from %s:%s\n(incompatible JACK settings on sender: SR: %d). telling sender to stop.\n",
					lo_address_get_hostname(loa),lo_address_get_port(loa),remote_sample_rate
				);

				message_number=0;
				message_number_prev=0;
				remote_sample_rate=0;
				remote_period_size=0;
//				pre_buffer_counter=0;
			}
			else
			{

				lo_address loa;

				if(use_tcp==1)
				{
					lo_address loa_ = lo_message_get_source(data);
					loa = lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
				}
				else
				{
					loa = lo_message_get_source(data);
				}

				fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings on sender: SR: %d.\nshutting down (see option --close)...\n",
					lo_address_get_hostname(loa),lo_address_get_port(loa),remote_sample_rate
				);

				shutdown_in_progress=1;
				return 0;
			}
		}

		remote_period_size=lo_blob_datasize((lo_blob)argv[0+data_offset])/bytes_per_sample;
		fprintf(stderr,"\nsender was (re)started. ");

		if(remote_period_size!=period_size)
		{
			fprintf(stderr,"sender period size: %d samples (%.3f x local)\n\n",remote_period_size,(float)remote_period_size/period_size);
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

	if(pre_buffer_counter >= pre_buffer_size && process_enabled == 0)
	{
		//if buffer filled, start to output audio in process()
		process_enabled=1;
	}

	int mc_period_bytes=period_size*bytes_per_sample*port_count;

	//check if a whole mc period can be written to the ringbuffer
	size_t can_write_count=jack_ringbuffer_write_space(rb);
	if(can_write_count < mc_period_bytes)
	{
			buffer_overflow_counter++;
			/////////////////
			fprintf(stderr,"\rBUFFER OVERFLOW! this is bad -----%s","\033[0J");
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
			int cnt=jack_ringbuffer_write(rb, (void *) data, 
				period_size*bytes_per_sample);
		}
		pre_buffer_counter++;
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
			int cnt=jack_ringbuffer_write(rb_helper, (void *) data, 
				remote_period_size*bytes_per_sample);
		}

		//if enough data collected for one larger multichannel period

		while(jack_ringbuffer_read_space(rb_helper)	>=mc_period_bytes
		&& jack_ringbuffer_write_space(rb)		>=mc_period_bytes)
		{
			//transfer from helper to main ringbuffer
			unsigned char* data;
			data=malloc(				mc_period_bytes);
			//store orig pointer
			unsigned char* orig_data=data;
			jack_ringbuffer_read(rb_helper,data,	mc_period_bytes);

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
					int w=jack_ringbuffer_write(rb,(void *)data,remote_period_size*bytes_per_sample);
				}
			}
			data=orig_data;
			free(data);

			pre_buffer_counter++;
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

				int cnt=jack_ringbuffer_write(rb, (void *) data, 
					period_size*bytes_per_sample);
			}
			pre_buffer_counter++;
		}
	}

	return 0;
}//end audio_handler

// /buffer
int buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	if(allow_remote_buffer_control==0)
	{
		fprintf(stderr,"\nremote buffer control /buffer ii disabled! ignoring.\n");
		return 0;
	}

	int pre_buffer_periods=fmax(1,argv[0]->i);
	int max_buffer_periods=fmax(pre_buffer_periods,argv[1]->i);

	fprintf(stderr,"\n/buffer received pre,max: %d, %d\n",pre_buffer_periods,max_buffer_periods);

	size_t rb_size=max_buffer_periods
		*output_port_count*bytes_per_sample*period_size;

	//create new buffer if not equal to current max
	//the current buffer will be lost
	if(max_buffer_periods != max_buffer_size)
	{
		char buf[64];
		format_seconds(buf,(float)max_buffer_periods*period_size/(float)sample_rate);

		fprintf(stderr,"new ringbuffer size: %d mc periods (%s, %zu bytes, %.2f mb)\n",
			max_buffer_periods,
			buf,
			rb_size,
			(float)rb_size/1000/1000
		);

		max_buffer_size=max_buffer_periods;

		rb=jack_ringbuffer_create (rb_size);
		// /buffer is experimental, it can segfault
	}

	//current size
	size_t can_read_count = jack_ringbuffer_read_space(rb);
	size_t can_read_periods_count = can_read_count/port_count/period_size/bytes_per_sample;

	if(pre_buffer_periods>can_read_periods_count)
	{
		//fill buffer
		size_t fill_periods_count=pre_buffer_periods-can_read_periods_count;
		fprintf(stderr,"-> FILL %zu\n",fill_periods_count);

		pre_buffer_size=fill_periods_count;
		pre_buffer_counter=0;
		process_enabled=0;
	}
	else if(pre_buffer_periods<can_read_periods_count)
	{
		//do in process() (reader)
		requested_drop_count+=can_read_periods_count-pre_buffer_periods;
		fprintf(stderr," -> DROP %zu\n",requested_drop_count);
	}
	
	return 0;
}//end buffer_handler
