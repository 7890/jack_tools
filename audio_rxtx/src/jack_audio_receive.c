/* jack_audio_receive -- receive uncompressed audio from another host via OSC
 *
 * Copyright (C) 2013 Thomas Brand <tom@trellis.ch>
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
#ifndef WIN32
#include <unistd.h>
#endif
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>

#include "jack_audio_common.h"

//asprintf is a GNU extensions that is only declared when __GNU_SOURCE is set
#define __GNU_SOURCE

//tb/130427/131206//131211//131216/131229
//gcc -o jack_audio_receiver jack_audio_receiver.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

//between incoming osc messages and jack process() callbacks
jack_ringbuffer_t *rb;

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

//insert(copy last): positive number
//drop samples: negative number
/*
float sample_drift_per_second=0;
float sample_drift_per_cycle=0;
float sample_drift_sum=0;
*/

//ctrl+c etc
static void signal_handler(int sig)
{
	fprintf(stderr,"\nterminate signal %d received, telling sender to pause.\n",sig);

	lo_address loa = lo_address_new(sender_host,sender_port);
	lo_message msg=lo_message_new();
	lo_send_message (loa, "/pause", msg);
	lo_message_free(msg);

	shutdown_in_progress=1;
	process_enabled=0;

	fprintf(stderr,"cleaning up...");

	jack_client_close(client);
	lo_server_thread_free(lo_st);
	jack_ringbuffer_free(rb);

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

int trip_handler(const char *path, const char *types, lo_arg **argv, int argc,
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

	//this is per channel, not per cycle. *port_count
	if(relaxed_display_counter>=update_display_every_nth_cycle*port_count
		|| last_test_cycle==1
	)
	{
		fprintf(stderr,"\r# %" PRId64 " i: %d f: %.1f b: %zu s: %.4f i: %.2f r: %" PRId64 
			" l: %" PRId64 " d: %" PRId64 " o: %" PRId64 " p: %.1f%s",/* d: %.1f%s",*/

			message_number,
			input_port_count,
			(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count,
			can_read_count,
			(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate,
			time_interval_avg*1000,
			remote_xrun_counter,local_xrun_counter,
			multi_channel_drop_counter,
			buffer_overflow_counter,
			(float)frames_since_cycle_start_avg/(float)period_size,
			/*sample_drift_sum,*/
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

		//sample_drift_sum+=sample_drift_per_cycle;
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
	fprintf (stderr, "Usage: jack_audio_receive <Options> <Listening port>.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                 --help\n");
	fprintf (stderr, "  Number of playback channels:   (2) --out      <integer>\n");
	fprintf (stderr, "  Autoconnect ports:           (off) --connect\n");
	fprintf (stderr, "  Jack client name:        (receive) --name     <string>\n");
	fprintf (stderr, "  Initial buffer size:(4 mc periods) --pre      <integer>\n");
	fprintf (stderr, "  Max buffer size >= init:    (auto) --mbuff    <integer>\n");
	fprintf (stderr, "  Re-use old data on underflow: (no) --nozero\n");
//	fprintf (stderr, "  Sample drift per second:       (0) --drift    <float +/->\n");
	fprintf (stderr, "  Update info every nth cycle   (99) --update   <integer>\n");
	fprintf (stderr, "  Limit processing count:      (off) --limit    <integer>\n");
	fprintf (stderr, "Listening port:   <integer>\n\n");
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
	jack_options_t options = JackNullOption;
	jack_status_t status;

	//osc
	const char *listenPort;

	//command line options parsing
	//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
	static struct option long_options[] =
	{
		{"help",	no_argument,		0, 'h'},
		{"out",		required_argument, 	0, 'o'},
		{"connect",	no_argument,	&autoconnect, 1},
		{"name",	required_argument,	0, 'n'},
		{"pre",		required_argument,	0, 'b'},//pre (delay before playback) buffer
		{"mbuff",	required_argument,	0, 'm'},//max (allocate) buffer
		{"nozero",	no_argument,	&zero_on_underflow, 'z'},
		/*{"drift",	required_argument,	0, 'd'},*/
		{"update",	required_argument,	0, 'u'},
		{"limit",	required_argument,	0, 't'},//test, stop after n processed
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

			case 'n':
				client_name=optarg;
				break;

			case 'b':
				pre_buffer_size=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'm':
				max_buffer_size=fmax(1,(uint64_t)atoll(optarg));
				break;

/*
			case 'd':
				sample_drift_per_second=(float)atof(optarg);
				break;
*/

			case 'u':
				update_display_every_nth_cycle=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 't':
				receive_max=fmax(1,(uint64_t)atoll(optarg));
				test_mode=1;
				fprintf(stderr,"*** limiting number of messages: %" PRId64 "\n",receive_max);

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
	client = jack_client_open (client_name, options, &status, server_name);
	if (client == NULL) {
		fprintf(stderr,"jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf(stderr,"Unable to connect to JACK server\n");
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

	//sample_drift_per_cycle=(float)sample_drift_per_second/((float)sample_rate/(float)period_size);

	//print startup info

	fprintf(stderr,"listening on osc port: %s\n",listenPort);

	read_jack_properties();
	print_common_jack_properties();

	fprintf(stderr,"channels (playback): %d\n",output_port_count);

	fprintf(stderr,"multi-channel period size: %d bytes\n",
		output_port_count*period_size*bytes_per_sample
	);

	char *strat="fill with zero (silence)";
	if(zero_on_underflow==0)
	{
		strat="re-use last available period";
	}

	//fprintf(stderr,"sample drift: %.1f samples per second\n", sample_drift_per_second);

	fprintf(stderr,"underflow strategy: %s\n",strat);

#if 0 //ndef __APPLE__
	fprintf(stderr,"free memory: %" PRId64 " mb\n",get_free_mem()/1000/1000);
#endif

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
	fprintf(stderr,"allocated buffer size: %zu mc periods (%s, %zu bytes, %.2f mb)\n\n",
		max_buffer_size,
		buf,
		rb_size,
		(float)rb_size/1000/1000
	);
	buf[0] = '\0';

#if 0//ndef __APPLE__
	if(rb_size > get_free_mem())
	{
		fprintf(stderr,"not enough memory to create the ringbuffer.\n");
		fprintf(stderr,"try --mbuff <smaller size>.\n");
		exit(1);
	}
#endif

	//====================================
	rb = jack_ringbuffer_create (rb_size);

	if(rb==NULL)
	{
		fprintf(stderr,"could not create a ringbuffer with that size.\n");
		fprintf(stderr,"try --mbuff <smaller size>.\n");
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
	ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) 
	{
		fprintf(stderr,"no physical playback ports\n");
		//exit (1);
	}
	
	int connection_port_count=fmin(output_port_count,sizeof(ports));

	if(autoconnect==1)
	{
		int i;
		for(i=0;i<connection_port_count;i++)
		{
			if (jack_connect (client, jack_port_name(ioPortArray[i]), ports[i])) 
			{
				fprintf (stderr, "autoconnect: could not connect output port %d to %s\n",i,
						jack_port_name(ioPortArray[i])
				);
			}
			else
			{
				fprintf (stderr, "autoconnect: output port %d connected to %s\n",i,jack_port_name(ioPortArray[i]));
			}
		}

		free (ports);
	}

	/* install a signal handler to properly quits jack client */
#ifdef WIN32
	signal(SIGINT, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGTERM, signal_handler);
#else
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
#endif

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

	#ifdef WIN32 
		Sleep(1000);
	#else
		sleep (1);
	#endif
	}

	exit (0);
}

void registerOSCMessagePatterns(const char *port)
{
	/* osc server */
	lo_st = lo_server_thread_new(port, error);

/*
	/offer iiiifh

	1) i: sample rate
	2) i: bytes per sample
	3) i: period size
	4) i: channel count
	5) f: expected network data rate
	6) h: send / request counter
*/

	lo_server_thread_add_method(lo_st, "/offer", "iiiifh", offer_handler, NULL);

/*
	/trip it

	1) i: id/sequence/any number that will be replied
	2) t: timestamp from sender that will be replied
*/

	lo_server_thread_add_method(lo_st, "/trip", "it", trip_handler, NULL);

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
	/audio hhtb*

	1) h: message number
	2) h: xrun counter (sender side, as all the above meta data)
	3) t: timetag containing seconds since 1970 and usec fraction
	4) b: blob of channel 1 (period size * bytes per sample) bytes long
	...
	11) b: up to 8 channels
*/

	//support 1-8 blobs / channels per message
	lo_server_thread_add_method(lo_st, "/audio", "hhtb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbb", audio_handler, NULL);
//8
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbb", audio_handler, NULL);
//16
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
//32

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
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

	int offered_sample_rate=argv[0]->i;
	int offered_bytes_per_sample=argv[1]->i;
	int offered_period_size=argv[2]->i;
	int offered_channel_count=argv[3]->i;
	float offered_data_rate=argv[4]->f;
	uint64_t request_counter=argv[5]->h;

	lo_message msg=lo_message_new();

	//send back to host that offered audio
	lo_address loa = lo_message_get_source(data);

	//check if compatible with sender
	//could check more stuff (channel count, data rate, sender host/port, ...)
	if(
		offered_sample_rate==sample_rate
		&& offered_bytes_per_sample==bytes_per_sample
		&& offered_period_size==period_size
	)
	{
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
	else
	{
		//sending deny will tell sender to stop/quit
		lo_send_message (loa, "/deny", msg);

		fprintf(stderr,"\ndenying transmission from %s:%s (incompatible jack settings on sender: SR: %d period size %d). telling sender to stop.\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa),offered_sample_rate,offered_period_size
		);

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

/*
	lo_address loa = lo_message_get_source(data);
	strcpy(sender_host,lo_address_get_hostname(loa));
	strcpy(sender_port,lo_address_get_port(loa));
	fprintf(stderr,"receiving from %s:%s",
		lo_address_get_hostname(loa),lo_address_get_port(loa)
	);
*/

	//the messages are numbered sequentially. first msg is numberd 1
	message_number=argv[0]->h;

	if(message_number_prev>message_number)
	{
		printf("\nsender was restarted\n");
	}

	message_number_prev=message_number;

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

	//first blob is at data_offset+1 (one-based)
	int data_offset=3;

	//total args count minus metadata args count = number of blobs
	input_port_count=argc-data_offset;

	//only process useful number of channels
	port_count=fmin(input_port_count,output_port_count);

	if(pre_buffer_counter >= pre_buffer_size && process_enabled == 0)
	{
		//if buffer filled, start to output audio in process()
		process_enabled=1;
	}

	//check if a whole mc period can be written to the ringbuffer
	size_t can_write_count=jack_ringbuffer_write_space(rb);
	if(can_write_count < port_count*period_size*bytes_per_sample)
	{
			buffer_overflow_counter++;
			/////////////////
			fprintf(stderr,"\rBUFFER OVERFLOW! this is bad -----%s","\033[0J");
			return 0;
	}

	int i;
	//don't read more channels than we have outputs
	for(i=0;i < port_count;i++)
	{
		//get blob (=one period of one channel)
		unsigned char *data = lo_blob_dataptr((lo_blob)argv[i+data_offset]);
		//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

		size_t can_write_count=jack_ringbuffer_write_space(rb);

		//write to ringbuffer
		//==========================================
		int cnt=jack_ringbuffer_write(rb, (void *) data, 
			period_size*bytes_per_sample);

	}
	pre_buffer_counter++;

	return 0;
}//end audio_handler

// /trip
//not used for now
//send back with local time received timetag
int trip_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

//	fprintf(stderr,"\rtripping...");

	gettimeofday(&tv, NULL);
	lo_timetag tt;
	tt.sec=tv.tv_sec;
	tt.frac=tv.tv_usec;

	lo_address loa = lo_message_get_source(data);
	lo_message msg=lo_message_new();

	lo_message_add_int32(msg,argv[0]->i);
	lo_message_add_timetag(msg,argv[1]->t);
	lo_message_add_timetag(msg,tt);
	lo_send_message (loa, "/trip", msg);

	lo_message_free(msg);

	return 0;
}

// /buffer
int buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
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
