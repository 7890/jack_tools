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

//tb/130427/131206//131211
//gcc -o jack_audio_receiver jack_audio_receiver.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

float version = 0.43;

jack_client_t *client;

//Array of pointers to input ports
jack_port_t **outputPortArray;

// Array of pointers to input buffers
jack_default_audio_sample_t **inputBufferArray;

//between incoming osc messages and jack process() callbacks
jack_ringbuffer_t *rb;

//connect output to first n physical channels on startup
int autoconnect=0;

//just to test
int max_channel_count=64;

//default values
//sample_rate, period_size and bytes_per_sample must be
//THE SAME on sender and receiver
int input_port_count=2; //we can't know yet
int output_port_count=2; //param
int port_count=2; //minimum of in/out
int sample_rate=44100;
int period_size=512;
int bytes_per_sample=4;

//fill n periods to buffer before processing (sending to jack)
size_t pre_buffer_size=4; //param
size_t pre_buffer_counter=0;

//indicating how many periods to drop (/buffer)
size_t requested_drop_count=0;

//will be enabled when pre_buffer filled
int process_enabled=0;

//osc
lo_server_thread st;

char sender_host[255];
char sender_port[10];

//defined by sender
uint64_t message_number=0;

//to detect gaps
uint64_t message_number_prev=0;

//temporary counter (will be reset for avg calc)
uint64_t msg_received_counter=0;

//count what we process
uint64_t process_cycle_counter=0;

uint64_t remote_xrun_counter=0;
uint64_t local_xrun_counter=0;
uint64_t ringbuffer_underflow_counter=0;

int shutdown_in_progress=0;
int starting_transmission=0;

//to capture current time
struct timeval tv;
lo_timetag tt_prev;

//misc measurement
//seconds
float time_interval=0;
float time_transmission=0;

double time_interval_sum=0;
float time_interval_avg=0;

double time_transmission_sum=0;
float time_transmission_avg=0;

//reset avg sum very 100 periods (avoid "slow" comeback)
int avg_calc_interval=100;

//if no data is available to give to jack, fill with zero (silence)
//if set to 0, the current wavetable will be used
//if the network cable is plugged out, this can sound awful
int zero_on_underflow=1; //param

//test_mode (--limit) is handy for testing purposes
//if set to 1, program will terminate after receiving receive_max messages
int test_mode=0;
uint64_t receive_max=10000;

//ctrl+c etc
static void signal_handler(int sig)
{
	lo_address loa = lo_address_new(sender_host,sender_port);
	if(loa!=NULL)
	{
		//tell sender to pause
		lo_message msg=lo_message_new();
		lo_send_message (loa, "/pause", msg);
		lo_message_free(msg);
	}

	jack_client_close(client);
	lo_server_thread_free(st);
	jack_ringbuffer_free(rb);
	fprintf(stderr,"\nterminate signal %d received, exit now.\n\n",sig);

	exit(0);
}

//osc handler
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

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int
process (jack_nframes_t nframes, void *arg)
{
	//if shutting down, fill buffers with 0 
	if(shutdown_in_progress==1)
	{
		int i;
		for( i=0; i < output_port_count; i++ )
		{
			jack_default_audio_sample_t *o1;
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (outputPortArray[i], nframes);
			memset ( o1, 0, bytes_per_sample*nframes );
		}

		return 0;
	}

	size_t cnt=0;
	size_t can_read_count=0;

	//if sender sends more channels than we have output channels, ignore them
	int i;
	for( i=0; i < port_count; i++ )
	{
		jack_default_audio_sample_t *o1;
		o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (outputPortArray[i], nframes);

		if(process_enabled==1)
		{
			//how many periods still ready to read in the buffer
			can_read_count = jack_ringbuffer_read_space(rb);

			if(can_read_count >=
					bytes_per_sample*nframes)
			{
				cnt=jack_ringbuffer_read (rb, (char*)o1, bytes_per_sample*nframes);
			}
			else
			{
				if(zero_on_underflow==1)
				{
					//skip for now (!) and set output buffer silent
					memset ( o1, 0, bytes_per_sample*nframes );
				}
				ringbuffer_underflow_counter++;
				time_interval_avg=0;
				time_transmission_avg=0;
			}

			/*
			fprintf(stderr,"receiving from %s:%s",
				sender_host,sender_port
			);
			*/

			fprintf(stderr,"\r# %" PRId64 " i: %d f: %.1f b: %lu s: %.4f",
				message_number,
				input_port_count,
				(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count,
				can_read_count,
				(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate
			);

			/*
			const char *alert="";
			if(time_transmission_avg<0)
			{
				//this happens if the sender's clock is late
				//best results when both hosts are using ntp
				alert="!";
			}
			*/

			fprintf(stderr," i: %.2f",// t: %.2f%s x: %.1f%s",
				time_interval_avg*1000
				//time_transmission_avg*1000,alert,
				//time_interval_avg/time_transmission_avg,alert
			);

			fprintf(stderr," r: %" PRId64 " l: %" PRId64 
				" u: %" PRId64 "",
				remote_xrun_counter,local_xrun_counter,
				ringbuffer_underflow_counter/port_count
			);

			fprintf(stderr,"%s", "\033[0J");

		} // end if process enabled
		//process not yet enabled, buffering
		else
		{
			//only for init
			if((int)message_number<=0 && starting_transmission==0)
			{
				fprintf(stderr,"\rwaiting for audio input data...");
			}
			else
			{
				fprintf(stderr,"\r# %" PRId64 " buffering... mc periods to go: %lu",
					message_number,pre_buffer_size-pre_buffer_counter
				);
			}
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
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (outputPortArray[input_port_count+i], nframes);
		}
	}

	if(process_enabled==1)
	{
		process_cycle_counter++;

		if(process_cycle_counter>=receive_max && test_mode==1)
		{
			fprintf(stderr,"\ntest finished after %" PRId64 " process cycles\n",process_cycle_counter);

			shutdown_in_progress=1;
			return 0;
		}

		//requested via /buffer, for test purposes (make buffer "tight")
		if(requested_drop_count>0)
		{
			size_t drop_bytes_count=requested_drop_count
				*port_count*period_size*bytes_per_sample;

			//create throw away buffer
			void *membuf = malloc(drop_bytes_count);

			jack_ringbuffer_read (rb, (char*)membuf, drop_bytes_count);

			free(membuf);

			requested_drop_count=0;
		}
	}
	return 0;
} //end process()

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
	lo_server_thread_free(st);
	exit (1);
}

static void header (void)
{
	fprintf (stderr, "\njack_audio_receive v%.2f\n", version);
	fprintf (stderr, "(C) 2013 Thomas Brand  <tom@trellis.ch>\n");
}

static void help (void)
{
	fprintf (stderr, "Usage: jack_audio_receive <Options> <Listening port>.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                 --help\n");
	fprintf (stderr, "  Number of playback channels:   (2) --out <number>\n");
	fprintf (stderr, "  Autoconnect ports:           (off) --connect\n");
	fprintf (stderr, "  Jack client name:      (prg. name) --name <string>\n");
	fprintf (stderr, "  Initial buffer size:(2 mc periods) --pre <number>\n");
	fprintf (stderr, "  Re-use old data on underflow: (no) --nozero\n");
	fprintf (stderr, "  Limit processing count:      (off) --limit <number>\n");
	fprintf (stderr, "Listening port:   <number>\n\n");
	fprintf (stderr, "Example: jack_audio_receive --in 8 --connect --pre 200 1234\n");
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
		{"out",		required_argument,	0, 'i'},
		{"connect",	no_argument,	&autoconnect, 1},
		{"name",	required_argument,	0, 'n'},
		{"pre",		required_argument,	0, 'b'},//pre buffer
		{"nozero",	no_argument,	&zero_on_underflow, 'z'},
		{"limit",	required_argument,	0, 't'},//test, stop after n processed
		{0, 0, 0, 0}
	};

	//print program header
	header();

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
				help();
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
				pre_buffer_size=(uint64_t)atoll(optarg);
				break;


			case 't':
				receive_max=(uint64_t)atoll(optarg);
				test_mode=1;
				fprintf(stderr,"*** limiting number of messages: %" PRId64 "\n",receive_max);

				break;

			case '?': //invalid commands
				/* getopt_long already printed an error message. */
				fprintf (stderr, "Weird arguments, try --help.\n\n");
				exit(1);

				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameters listening port
	if(argc-optind != 1)
	{
		fprintf (stderr, "Weird arguments, try --help.\n\n");
		exit(1);
	}

	listenPort=argv[optind];

	//initialize time
	gettimeofday(&tv, NULL);
	tt_prev.sec=tv.tv_sec;
	tt_prev.frac=tv.tv_usec;

	//create an array of input ports
	outputPortArray = (jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	inputBufferArray = (jack_default_audio_sample_t**) malloc(output_port_count * sizeof(jack_default_audio_sample_t*));

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

	fprintf(stderr,"listening on osc port: %s\n",listenPort);

	sample_rate=jack_get_sample_rate(client);
	fprintf(stderr,"sample rate: %d\n",sample_rate);

	bytes_per_sample = sizeof(jack_default_audio_sample_t);
	fprintf(stderr,"bytes per sample: %d\n",bytes_per_sample);

	period_size=jack_get_buffer_size(client);
	fprintf(stderr,"period size: %d samples (%.2f ms, %d bytes)\n",period_size,
		1000*(float)period_size/(float)sample_rate,
		period_size*bytes_per_sample
	);

	fprintf(stderr,"channels (playback): %d\n",output_port_count);

	fprintf(stderr,"max. multi-channel period size: %d bytes\n",
		output_port_count*period_size*bytes_per_sample
	);

	char *strat="fill with zero (silence)";
	if(zero_on_underflow==0)
	{
		strat="re-use last available period";
	}

	fprintf(stderr,"underflow strategy: %s\n",strat);

	fprintf(stderr,"initial buffer: %lu mc periods (%.4f sec)\n",pre_buffer_size,
		(float)pre_buffer_size*period_size/(float)sample_rate
	);

	//avoid 0
	int factor=1;
	if(pre_buffer_size>1)
	{
		factor*=pre_buffer_size;
	}

	size_t rb_size=10*output_port_count*bytes_per_sample*period_size*factor;//*pre_buffer_size;
	fprintf(stderr,"ringbuffer: %lu bytes\n\n",rb_size);

	//make a ringbuffer, large enough
	rb = jack_ringbuffer_create (rb_size);

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/
	//NULL could be config/data struct
	jack_set_process_callback (client, process, NULL);

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
		outputPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (outputPortArray[port] == NULL) 
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
		exit (1);
	}
	
	int connection_port_count=fmin(output_port_count,sizeof(ports));

	if(autoconnect==1)
	{
		int i;
		for(i=0;i<connection_port_count;i++)
		{
			if (jack_connect (client, jack_port_name(outputPortArray[i]), ports[i])) 
			{
				fprintf (stderr, "cannot connect output port %s\n",
						jack_port_name(outputPortArray[i])
				);
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

	//start osc server
	registerOSCMessagePatterns(listenPort);

	lo_server_thread_start(st);

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
	st = lo_server_thread_new(port, error);

/*
	/offer typestring: iiiifh

	1) i: sample rate
	2) i: bytes per sample
	3) i: period size
	4) i: channel count
	5) f: expected network data rate
	6) h: send / request counter
*/

	lo_server_thread_add_method(st, "/offer", "iiiifh", offer_handler, NULL);

/*
	/trip typestring: it

	1) i: id/sequence/any number that will be replied
	2) t: timestamp from sender that will be replied
*/

	lo_server_thread_add_method(st, "/trip", "it", trip_handler, NULL);

/*
	experimental

	/buffer typestring: i

	1) i: target value for available periods in ringbuffer (multi channel)
	drop or add periods
	this will introduce a hearable click on drops
	or pause playback for rebuffering depending on the current buffer fill
*/

	lo_server_thread_add_method(st, "/buffer", "i", buffer_handler, NULL);

/*
	/audio typestring: hhtb*

	1) h: message number
	2) h: xrun counter (sender side, as all the above meta data)
	3) t: timetag containing seconds since 1970 and usec fraction
	4) b: blob of channel 1 (period size * bytes per sample) bytes long
	...
	11) b: up to 8 channels
*/

	//support 1-8 blobs / channels per message
	lo_server_thread_add_method(st, "/audio", "hhtb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbb", audio_handler, NULL);
//8
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbb", audio_handler, NULL);
//16
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
//32

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);

	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
	lo_server_thread_add_method(st, "/audio", "hhtbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", audio_handler, NULL);
//64. ouff. maybe using a generic handler?
//this is a theoretical value, working on localhost at best
//to test 64 channels, use a small period size

}

//osc handlers
void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d in path %s: %s\n", num, path, msg);
}

//not used for now
//send back with local time received timetag
int trip_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	//don't accept if shutdown is ongoing
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

	if(loa!=NULL)
	{
		lo_message_add_int32(msg,argv[0]->i);
		lo_message_add_timetag(msg,argv[1]->t);
		lo_message_add_timetag(msg,tt);
		lo_send_message (loa, "/trip", msg);
	}

	lo_message_free(msg);

	return 0;
}

int buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	//don't accept if shutdown is ongoing
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	//target size
	int buffer_periods=fmax(1,argv[0]->i);

	fprintf(stderr,"\n/buffer received: %d",buffer_periods);

	//current size
	size_t can_read_count = jack_ringbuffer_read_space(rb);
	size_t can_read_periods_count = can_read_count/port_count/period_size/bytes_per_sample;

	if(buffer_periods>can_read_periods_count)
	{
		//fill buffer
		size_t fill_periods_count=buffer_periods-can_read_periods_count;
		fprintf(stderr," -> FILL %lu\n",fill_periods_count);

		pre_buffer_size=fill_periods_count;
		pre_buffer_counter=0;
		process_enabled=0;
	}
	else if(buffer_periods<can_read_periods_count)
	{
		//do in process() (reader)
		requested_drop_count+=can_read_periods_count-buffer_periods;
		fprintf(stderr," -> DROP %lu\n",requested_drop_count);
	}
	
	return 0;
}//end buffer_handler

int offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{

	//don't accept if shutdown is ongoing
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
	//lo_address loa = lo_address_new("localhost","1234");
	lo_address loa = lo_message_get_source(data);

	//check if compatible with sender
	//could check more stuff (channel count, data rate, sender host/port, ...)
	if(
		loa!=NULL
		&& offered_sample_rate==sample_rate
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

		fprintf(stderr,"\ndenying transmission from %s:%s (incompatible jack settings).\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa)
		);

		shutdown_in_progress=1;
	}

	lo_message_free(msg);

	return 0;
} //end offer_handler

int audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{

	//don't handle if shutdown is ongoing
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	msg_received_counter++;

	gettimeofday(&tv, NULL);

	lo_address loa = lo_message_get_source(data);
	if(loa!=NULL)
	{
		strcpy(sender_host,lo_address_get_hostname(loa));
		strcpy(sender_port,lo_address_get_port(loa));
		/*
		fprintf(stderr,"receiving from %s:%s",
			lo_address_get_hostname(loa),lo_address_get_port(loa)
		);
		*/
	}

	//the messages are numbered sequentially
	message_number=argv[0]->h;

	if(message_number_prev>message_number)
	{
		printf("\nsender was restarted! gap is %" PRId64 "\n",
			message_number-message_number_prev
		);
	}

	message_number_prev=message_number;

	remote_xrun_counter=argv[1]->h;

	lo_timetag tt=argv[2]->t;

	double msg_time=tt.sec+(double)tt.frac/1000000;
	double msg_time_prev=tt_prev.sec+(double)tt_prev.frac/1000000;
	double time_now=tv.tv_sec+(double)tv.tv_usec/1000000;

	time_interval=msg_time-msg_time_prev;
	time_transmission=time_now-msg_time;

	time_interval_sum+=time_interval;
	time_interval_avg=(float)time_interval_sum/msg_received_counter;

	time_transmission_sum+=time_transmission;
	time_transmission_avg=(float)time_transmission_sum/msg_received_counter;

	/*
	fprintf(stderr," tdiff ms avg: %.2f %.2f %.2f",
		time_interval_avg*1000, time_transmission_avg*1000,
		time_interval_avg/time_transmission_avg
	);
	*/

	tt_prev=tt;

	//reset avg calc
	if(msg_received_counter>=avg_calc_interval)
	{
		msg_received_counter=1;
		time_interval_sum=time_interval;
		time_transmission_sum=time_transmission;
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

	int i;
	//don't read more channels than we have outputs
	for(i=0;i < port_count;i++)
	{
		//get blob (=one period of one channel)
		unsigned char *d = lo_blob_dataptr((lo_blob)argv[i+data_offset]);
		//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

		size_t can_write_count=jack_ringbuffer_write_space(rb);
		//if(can_write_count < port_count*period_size*bytes_per_sample)

		if(can_write_count < period_size*bytes_per_sample)
		{
			//////////////////
			fprintf(stderr,"\rBUFFER OVERFLOW! this is bad ----- ");
			fprintf(stderr,"%s", "\033[0J");

		}
		else
		{
			//write to ringbuffer
			//==========================================
			int cnt=jack_ringbuffer_write(rb, (void *) d, 
				period_size*bytes_per_sample);

		}
	}
	pre_buffer_counter++;

	return 0;
}//end audio_handler

//some docs
/*

size_t jack_ringbuffer_write 	( 	jack_ringbuffer_t *  	rb,
		const char *  	src,
		size_t  	cnt	 
	) 			

Write data into the ringbuffer.

Parameters:
    	rb 	a pointer to the ringbuffer structure.
    	src 	a pointer to the data to be written to the ringbuffer.
    	cnt 	the number of bytes to write.


memcpy

void * memcpy ( void * destination, const void * source, size_t num );

Copy block of memory
Copies the values of num bytes from the location pointed by source directly to the memory block pointed by destination.

The underlying type of the objects pointed by both the source and destination pointers are irrelevant for this function; The result is a binary copy of the data.

The function does not check for any terminating null character in source - it always copies exactly num bytes.

To avoid overflows, the size of the arrays pointed by both the destination and source parameters, shall be at least num bytes, and should not overlap (for overlapping memory blocks, memmove is a safer approach).

Parameters

destination
    Pointer to the destination array where the content is to be copied, type-casted to a pointer of type void*.
source
    Pointer to the source of data to be copied, type-casted to a pointer of type const void*.
num
    Number of bytes to copy.
    size_t is an unsigned integral type.


Return Value
destination is returned.


memset

void * memset ( void * ptr, int value, size_t num );

Fill block of memory
Sets the first num bytes of the block of memory pointed by ptr to the specified value (interpreted as an unsigned char).

Parameters

ptr
    Pointer to the block of memory to fill.
value
    Value to be set. The value is passed as an int, but the function fills the block of memory using the unsigned char conversion of this value.
num
    Number of bytes to be set to the value.
    size_t is an unsigned integral type.


Return Value
ptr is returned.


EXPORT size_t jack_ringbuffer_read 	( 	jack_ringbuffer_t *  	rb,
		char *  	dest,
		size_t  	cnt	 
	) 			

Read data from the ringbuffer.

Parameters:
    	rb 	a pointer to the ringbuffer structure.
    	dest 	a pointer to a buffer where data read from the ringbuffer will go.
    	cnt 	the number of bytes to read.

Returns:
    the number of bytes read, which may range from 0 to cnt. 

*/
