/* jack_audio_send -- send uncompressed audio to another host via OSC
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
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>

//tb/130427/131206//131211
//gcc -o jack_audio_send jack_audio_send.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

float version = 0.51;

jack_client_t *client;

//Array of pointers to input ports
jack_port_t **inputPortArray;

// Array of pointers to input buffers
jack_default_audio_sample_t **inputBufferArray;

//local xrun counter (since start of this jack client)
uint64_t xrun_counter=0;

//connect output to first n physical channels on startup
int autoconnect=0; //param

//this is a theoretical limit
int max_channel_count=64;

//default values
//sample_rate, period_size and bytes_per_sample must be
//THE SAME on sender and receiver
int input_port_count=2; //param
int sample_rate=44100;
int period_size=512;
int bytes_per_sample=4;

//will be enabled when pre_buffer filled
int process_enabled=0;

//osc
lo_server_thread st;
lo_address loa;

//for message numberings, 1-based
//will be reset to 1 on start of audio transmission
uint64_t msg_sequence_number=1;

int msg_size=0;
int transfer_size=0;
int max_transfer_size=32000;

//expected kbit/s that will arrive on receiver
float expected_network_data_rate=0;

//0: receiver denied  1: receiver accepted
int receiver_accepted=-1;

int shutdown_in_progress=0;

//to capture current time
struct timeval tv;

//for misc measurements
float trip_time_interval=0;
float trip_time_interval_sum=0;
float trip_time_interval_avg=0;
float host_to_host_time_offset=0;

//store after how many frames all work is done in process()
int frames_since_cycle_start=0;
int frames_since_cycle_start_sum=0;
int frames_since_cycle_start_avg=0;

//reset fscs avg sum every 88. cycle
int fscs_avg_calc_interval=88;

//temporary counter (will be reset for avg calc)
int fscs_avg_counter=0;

//don't stress the terminal with too many fprintfs in process()
int update_display_every_nth_cycle=99;
int relaxed_display_counter=0;

//give lazy display a chance to output current value for last cycle
int last_test_cycle=0;

//test_mode (--limit) is handy for testing purposes
//if set to 1, program will terminate after sending send_max messages
int test_mode=0;
uint64_t send_max=10000;

//ctrl+c etc
static void signal_handler(int sig)
{
	fprintf(stderr, "\nterminate signal %d received. cleaning up...",sig);

	shutdown_in_progress=1;

	jack_client_close(client);
	lo_server_thread_free(st);

	fprintf(stderr,"done\n");

	exit(0);
}

//osc handler
void error(int num, const char *msg, const char *path)
{
	//just print, don't exit for now
	fprintf(stderr,"\nliblo server error %d in path %s: %s\n", num, path, msg);
}

int accept_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int deny_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int pause_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int trip_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

void trip();

//create a dummy message, return size in bytes (message length)
//don't forget to update when changing the real message in process()
int message_size()
{
	lo_message msg=lo_message_new();
	lo_message_add_int64(msg,msg_sequence_number);
	lo_message_add_int64(msg,xrun_counter);

	gettimeofday(&tv, NULL);
	lo_timetag tt;
	tt.sec=tv.tv_sec;
	tt.frac=tv.tv_usec;
	lo_message_add_timetag(msg,tt);

	lo_blob blob[input_port_count];
	void* membuf = malloc(period_size*bytes_per_sample);

	int i;
	for( i=0; i<input_port_count; i++ )
	{
		blob[i]=lo_blob_new(period_size*bytes_per_sample,membuf);
		lo_message_add_blob(msg,blob[i]);		
	}

	int msg_size = lo_message_length(msg,"/audio");

	//free resources to keep memory clean
	lo_message_free (msg);
	free(membuf);

	for(i=0;i<input_port_count;i++)
	{
		lo_blob_free(blob[i]);
	}

	return msg_size;
}

void offer_audio_to_receiver()
{
	/*
	don't send any audio data until accepted by receiver

	typestring: iiiifh

	1) i: sample rate
	2) i: bytes per sample
	3) i: period size
	4) i: channel count
	5) f: expected network data rate
	6) h: send / request counter

	receiver should answer with /accept or /deny
	*/

	lo_message msg=lo_message_new();

	//tell metadata about the stream
	//clients can decide if they're compatible
	lo_message_add_int32(msg,sample_rate);
	lo_message_add_int32(msg,bytes_per_sample);
	lo_message_add_int32(msg,period_size);
	lo_message_add_int32(msg,input_port_count);
	lo_message_add_float(msg,expected_network_data_rate);

	//add message counter
	lo_message_add_int64(msg,msg_sequence_number);

	lo_send_message (loa, "/offer", msg);

	//free resources to keep memory clean
	lo_message_free (msg);
}

//jack calls this method on every xrun
int xrun()
{
	xrun_counter++;
	return 0;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int
process(jack_nframes_t nframes, void *arg)
{
	//if shutting down, don't process at all
	if(shutdown_in_progress==1)
	{
		return 0;
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

	if(process_enabled==1)
	{
		//no answer from receiver yet
		if(receiver_accepted==-1)
		{
			offer_audio_to_receiver();

			if(relaxed_display_counter>=update_display_every_nth_cycle
				|| last_test_cycle==1
			)
			{
				//print info "in-place" with \r
				fprintf(stderr,"\r# %" PRId64 " offering audio to %s:%s...",
					msg_sequence_number,
					lo_address_get_hostname(loa),
					lo_address_get_port(loa)
				);
				relaxed_display_counter=0;
			}
			relaxed_display_counter++;

			msg_sequence_number++;

			return 0;
		}//end if receiver not yet accepted

		if(test_mode==1 && msg_sequence_number>=send_max)
		{
			last_test_cycle=1;
		}

//don't forget to update the dummy message in message_size()
/*
		/audio typestring: hhtb*

		1) h: message number
		2) h: xrun counter (sender side, as all the above meta data)
		3) t: timetag containing seconds since 1970 and usec fraction 
		4) b: blob of channel 1 (period size * bytes per sample) bytes long
		...
		67) b: up to 64 channels
*/

		lo_message msg=lo_message_new();

		//add message counter
		lo_message_add_int64(msg,msg_sequence_number);
		//indicate how many xruns on sender
		lo_message_add_int64(msg,xrun_counter);

		//current timestamp
		gettimeofday(&tv, NULL);
		lo_timetag tt;
		tt.sec=(long)tv.tv_sec;
		tt.frac=(long)tv.tv_usec;
		lo_message_add_timetag(msg,tt);

		//blob array, holding one period per channel
		lo_blob blob[input_port_count];

		//add blob to message for every input channel
		int i;
		for( i=0; i<input_port_count; i++ )
		{
			jack_default_audio_sample_t *o1;

			//get "the" buffer
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (inputPortArray[i],nframes);

			//fill blob from buffer
			blob[i]=lo_blob_new(bytes_per_sample*nframes,o1);
			lo_message_add_blob(msg,blob[i]);		
		}

		//==================================
		lo_send_message (loa, "/audio", msg);

		//free resources to keep memory clean
		lo_message_free (msg);
		for(i=0;i<input_port_count;i++)
		{
			lo_blob_free(blob[i]);
		}

		//calculate elapsed time
		size_t seconds_elapsed_total=msg_sequence_number * period_size / sample_rate;
		size_t hours_elapsed_total=seconds_elapsed_total / 3600;
		size_t minutes_elapsed_total=seconds_elapsed_total / 60;

		size_t minutes_elapsed=minutes_elapsed_total % 60;
		size_t seconds_elapsed=seconds_elapsed_total % 60;

		if(relaxed_display_counter>=update_display_every_nth_cycle
			|| last_test_cycle==1
		)
		{
			//print info "in-place" with \r
			fprintf(stderr,"\r# %" PRId64 
				" (%02lu:%02lu:%02lu) xruns: %" PRId64 " bytes tx: %" PRId64 " p: %.1f %s",
				msg_sequence_number,hours_elapsed_total,minutes_elapsed,seconds_elapsed,
				xrun_counter,
				transfer_size*msg_sequence_number+140, //140: minimal offer/accept
				(float)frames_since_cycle_start_avg/(float)period_size,
				"\033[0J"
			);
			relaxed_display_counter=0;
		}
		relaxed_display_counter++;

		msg_sequence_number++;

	} //end process enabled

	if(last_test_cycle==1)
	{
		fprintf(stderr,"\ntest finished after %" PRId64 " messages\n",msg_sequence_number-1);
		fprintf(stderr,"(waiting and buffering messages not included)\n");

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
	lo_server_thread_free(st);
	exit (1);
}

static void header (void)
{
	fprintf (stderr, "\njack_audio_send v%.2f\n", version);
	fprintf (stderr, "(C) 2013 Thomas Brand  <tom@trellis.ch>\n");
}

static void help (void)
{
	fprintf (stderr, "Usage: jack_audio_send <Options> <Receiver host> <Receiver port>.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                 --help\n");
	fprintf (stderr, "  Local port:                 (9990) --lport <number>\n");
	fprintf (stderr, "  Number of capture channels:    (2) --in <number>\n");
	fprintf (stderr, "  Autoconnect ports:           (off) --connect\n");
	fprintf (stderr, "  Jack client name:      (prg. name) --name <string>\n");
	fprintf (stderr, "  Update info every nth cycle   (99) --update <number>\n");
	fprintf (stderr, "  Limit totally sent messages: (off) --limit <number>\n");
	fprintf (stderr, "Receiver host:   <string>\n");
	fprintf (stderr, "Receiver port:   <number>\n\n");
	fprintf (stderr, "Example: jack_audio_send --in 8 10.10.10.3 1234\n");
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
	const char *client_name="send"; //param
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;

	//osc
	const char *localPort="9990"; //param
	const char *sendToHost; //param
	const char *sendToPort; //param

	//command line options parsing
	//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
	static struct option long_options[] =
	{
		{"help",	no_argument,		0, 'h'},
		{"lport",	required_argument,	0, 'p'},
		{"in",		required_argument,	0, 'i'},
		{"connect",	no_argument,	&autoconnect, 1},
		{"name",	required_argument,	0, 'n'},
		{"update",      required_argument,      0, 'u'},
		{"limit",	required_argument,	0, 't'},
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
/*
			printf ("option %s", long_options[option_index].name);
			if (optarg)
			{
				printf (" with arg %s", optarg);
				printf ("\n");
				break;
			}
*/

			case 'h':
				help();
				break;

			case 'p':
				localPort=optarg;
				break;

			case 'i':
				input_port_count=atoi(optarg);

				if(input_port_count>max_channel_count)
				{
					fprintf(stderr,"*** limiting capture ports to %d, sry\n",max_channel_count);
					input_port_count=max_channel_count;
				}	
				break;

			case 'n':
				client_name=optarg;
				break;

			case 'u':
				update_display_every_nth_cycle=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 't':
				send_max=fmax(1,(uint64_t)atoll(optarg));
				test_mode=1;
				fprintf(stderr,"*** limiting number of messages: %" PRId64 "\n",send_max);

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

	//remaining non optional parameters target host, port
	if(argc-optind != 2)
	{
		fprintf (stderr, "Weird arguments, try --help.\n\n");
		exit(1);
	}

	sendToHost=argv[optind];
	sendToPort=argv[++optind];

	//osc server
	st = lo_server_thread_new(localPort, error);

	//destination address
	loa = lo_address_new(sendToHost, sendToPort);

	lo_server_thread_add_method(st, "/accept", "", accept_handler, NULL);
	lo_server_thread_add_method(st, "/deny", "", deny_handler, NULL);
	lo_server_thread_add_method(st, "/pause", "", pause_handler, NULL);
	lo_server_thread_add_method(st, "/trip", "itt", trip_handler, NULL);

	//create an array of input ports
	inputPortArray = (jack_port_t**) malloc(input_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	inputBufferArray = (jack_default_audio_sample_t**) malloc(input_port_count * sizeof(jack_default_audio_sample_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, options, &status, server_name);
	if (client == NULL) {
		fprintf (stderr, "jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		exit (1);
		}
	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf (stderr, "*** unique name `%s' assigned\n", client_name);
	}

	fprintf(stderr,"sending from osc port: %s\n",localPort);
	fprintf(stderr,"target host:port: %s:%s\n",sendToHost,sendToPort);

	sample_rate=jack_get_sample_rate(client);
	fprintf(stderr, "sample rate: %d\n",sample_rate);

	bytes_per_sample = sizeof(jack_default_audio_sample_t);
	fprintf(stderr, "bytes per sample: %d\n",bytes_per_sample);

	period_size=jack_get_buffer_size(client);
	fprintf(stderr, "period size: %d samples (%.2f ms, %d bytes)\n",period_size,
		1000*(float)period_size/(float)sample_rate,
		period_size*bytes_per_sample
	);

	fprintf(stderr, "channels (capture): %d\n",input_port_count);

	fprintf(stderr,"multi-channel period size: %d bytes\n",
		input_port_count*period_size*bytes_per_sample
	);

	fprintf(stderr, "message rate: %.1f packets/s\n",
		(float)sample_rate/(float)period_size
	);

	//message size in bytes, including all metadata and blobs
	msg_size=message_size();

	//size in bytes totally used to send over network
	//experimental, compared with iptraf, localhost
	//handshake (offer/accept/deny/pause) not counted
	//20 bytes ip header, 8 bytes udp header, +?
	transfer_size=14+28+msg_size;

	fprintf(stderr,"message length: %d bytes\n", msg_size);
	fprintf(stderr,"transfer length: %d bytes (%.1f %% overhead)\n", 
		transfer_size,
		100-100*(float)input_port_count*period_size*bytes_per_sample/(float)transfer_size
	);

	if(transfer_size>max_transfer_size)
	{
		fprintf(stderr,"sry, can't do. max transfer length: %d. reduce input channel count.\n",max_transfer_size);
		signal_handler(42);
	}

	expected_network_data_rate=(float)sample_rate/(float)period_size 
		* transfer_size
		* 8 / 1000;

	fprintf(stderr, "expected network data rate: %.1f kbit/s\n\n",
		expected_network_data_rate
	);

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
	int port=0;
	for (port=0 ; port<input_port_count ; port ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "input_%d", (port+1)) < 0) {
			fprintf(stderr, "Could not create portname for port %d\n", port);
			exit(1);
		}

		// Register the output port
		inputPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if (inputPortArray[port] == NULL) {
			fprintf(stderr, "Could not create input port %d\n", (port+1));
			exit(1);
		}
	}

	/* Tell the JACK server that we are ready to roll. Our
	 * process() callback will start running now. */
	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client\n\n");
		exit (1);
	}

	/* Connect the ports. You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running. Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */
	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports found\n");
		exit (1);
	}
	
	int connection_port_count=fmin(input_port_count,sizeof(ports));

	if(autoconnect==1)
	{
		int i;
		for(i=0;i<connection_port_count;i++)
		{
			if (jack_connect (client, ports[i],jack_port_name(inputPortArray[i]))) {
				fprintf (stderr, "cannot connect input port %s\n",jack_port_name(inputPortArray[i]));
			}
		}
	}

	free (ports);

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

	//start the osc server
	lo_server_thread_start(st);

	//trip();

	process_enabled=1;

	/* keep running until the Ctrl+C */
	while (1) 
	{
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

//not used for now
void trip()
{
	fprintf(stderr,"measure round trip latency...");

	//lo_address loa = lo_message_get_source(data);
	int i;
	for(i=0;i<100;i++)
	{
		gettimeofday(&tv, NULL);
		lo_timetag tt;
		tt.sec=tv.tv_sec;
		tt.frac=tv.tv_usec;

	       	lo_message msg=lo_message_new();
		lo_message_add_int32(msg,i);

		lo_message_add_timetag(msg,tt);

		lo_send_message (loa, "/trip", msg);
		//lo_message_free(msg);
	}

	fprintf(stderr,"done.\n");
}

// /trip itt
int trip_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	//don't accept if shutdown is ongoing
	if(shutdown_in_progress==1)
	{
		return 0;
	}
	gettimeofday(&tv, NULL);

	int id=argv[0]->i;
	lo_timetag tt_sent=argv[1]->t;
	lo_timetag tt_received_on_receiver=argv[2]->t;

	double sent_time=tt_sent.sec+(double)tt_sent.frac/1000000;
	double received_on_receiver_time=tt_received_on_receiver.sec+(double)tt_received_on_receiver.frac/1000000;
	double time_now=tv.tv_sec+(double)tv.tv_usec/1000000;

	trip_time_interval=time_now-sent_time;
	trip_time_interval_sum+=trip_time_interval;
	trip_time_interval_avg=(float)trip_time_interval_sum/id+1;

	host_to_host_time_offset=received_on_receiver_time-sent_time+((float)trip_time_interval_avg/2);

	//fprintf(stderr,"%d %d %f %f\n",tt_sent.sec,tt_sent.frac,trip_time_interval_avg,host_to_host_time_offset);

	return 0;
} //end trip_handler

// /accept
int accept_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	receiver_accepted=1;
	msg_sequence_number=1;
	return 0;
}

// /deny
int deny_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	process_enabled=0;
	fprintf(stderr,"\nreceiver did not accept audio.\n");
	shutdown_in_progress=1;
	return 0;
}

//  /pause
int pause_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	fprintf(stderr,"\nreceiver requested pause\n");
	receiver_accepted=-1;
	msg_sequence_number=1;
	return 0;
}
