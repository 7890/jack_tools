/* jack_audio_send -- send uncompressed audio to another host via OSC
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
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>

#include "jack_audio_common.h"

//tb/130427/131206/131211/140523
//gcc -o jack_audio_send jack_audio_send.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

int input_port_count=2; //param

//local xrun counter (since start of this jack client)
uint64_t xrun_counter=0;

//osc receiver address
lo_address loa; //param

//for message numberings, 1-based
//will be reset to 1 on start of audio transmission
uint64_t msg_sequence_number=1;

//limit messages sent
//"signaling" not included
uint64_t send_max=10000; //param

//osc message size in bytes
int msg_size=0;

//msg_size + more
int transfer_size=0;
int max_transfer_size=32000;

//expected kbit/s that will arrive on receiver
float expected_network_data_rate=0;

//0: receiver denied  1: receiver accepted
int receiver_accepted=-1;

//for misc measurements
float trip_time_interval=0;
float trip_time_interval_sum=0;
float trip_time_interval_avg=0;
float host_to_host_time_offset=0;

struct timeval tv;

int nopause=0;

//ctrl+c etc
static void signal_handler(int sig)
{
	fprintf(stderr, "\nterminate signal %d received. cleaning up...",sig);

	shutdown_in_progress=1;

	jack_client_close(client);
	lo_server_thread_free(lo_st);

	fprintf(stderr,"done\n");

	exit(0);
}

//osc handler
void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"\nliblo server error %d: %s\n", num, msg);
	exit(1);
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
	lo_message_add_int32(msg,1111);

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

	/offer fiiiifh

	1) f: audio rx/tx format version
	2) i: sample rate
	3) i: bytes per sample
	4) i: period size
	5) i: channel count
	6) f: expected network data rate
	7) h: send / request counter

	receiver should answer with /accept or /deny fi <incomp. receiver format version> <incomp. receiver SR>
	*/

	lo_message msg=lo_message_new();

	//tell metadata about the stream
	//clients can decide if they're compatible
	lo_message_add_float(msg,format_version);
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
		//no answer from receiver yet. 
		//skip offering messages (directly send audio) if in nopause mode
		if(receiver_accepted==-1 && nopause==0)
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
		/audio hhtib*

		1) h: message number
		2) h: xrun counter (sender side, as all the above meta data)
		3) t: timetag containing seconds since 1970 and usec fraction 
		4) i: sampling rate
		5) b: blob of channel 1 (period size * bytes per sample) bytes long
		...
		68) b: up to 64 channels
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
		lo_message_add_int32(msg,sample_rate);

		//blob array, holding one period per channel
		lo_blob blob[input_port_count];

		//add blob to message for every input channel
		int i;
		for( i=0; i<input_port_count; i++ )
		{
			jack_default_audio_sample_t *o1;

			//get "the" buffer
			o1 = (jack_default_audio_sample_t*)jack_port_get_buffer (ioPortArray[i],nframes);

			//fill blob from buffer
			blob[i]=lo_blob_new(bytes_per_sample*nframes,o1);
			lo_message_add_blob(msg,blob[i]);		
		}

		//==================================
		lo_send_message (loa, "/audio", msg);
		//fprintf(stderr,"msg size %d\n",lo_message_length(msg,"/audio"));

		//free resources to keep memory clean
		lo_message_free (msg);
		for(i=0;i<input_port_count;i++)
		{
			lo_blob_free(blob[i]);
		}

		if(relaxed_display_counter>=update_display_every_nth_cycle
			|| last_test_cycle==1
		)
		{
			char hms[16];
			periods_to_HMS(hms,msg_sequence_number);

			//print info "in-place" with \r
			fprintf(stderr,"\r# %" PRId64 
				" (%s) xruns: %" PRId64 " tx: %" PRId64 " bytes (%.2f mb) p: %.1f%s",
				msg_sequence_number,hms,
				xrun_counter,
				transfer_size*msg_sequence_number,//+140, //140: minimal offer/accept
				(float)(transfer_size*msg_sequence_number)/1000/1000,//+140)/1000/1000,
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
	lo_server_thread_free(lo_st);
	exit (1);
}

static void print_help (void)
{
	fprintf (stderr, "Usage: jack_audio_send <Options> <Receiver host> <Receiver port>.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                  --help\n");
	fprintf (stderr, "  Local port:                  (9990) --lport  <integer>\n");
	fprintf (stderr, "  Number of capture channels :    (2) --in     <integer>\n");
	fprintf (stderr, "  Autoconnect ports:            (off) --connect\n");
	fprintf (stderr, "  Jack client name:            (send) --name   <string>\n");
	fprintf (stderr, "  Update info every nth cycle    (99) --update <integer>\n");
	fprintf (stderr, "  Limit totally sent messages:  (off) --limit  <integer>\n");
	fprintf (stderr, "  Immediate send, ignore /pause (off) --nopause\n");
	fprintf (stderr, "  (Use with multiple receivers. Ignore /pause, /deny)\n");
	fprintf (stderr, "Receiver host:   <string>\n");
	fprintf (stderr, "Receiver port:   <integer>\n\n");
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
		{"update",	required_argument,	0, 'u'},
		{"limit",	required_argument,	0, 't'},
		{"nopause",	no_argument,	&nopause, 1},
		{0, 0, 0, 0}
	};

	//print program header
	print_header("jack_audio_send");

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
				fprintf (stderr, "Wrong arguments, try --help.\n\n");
				exit(1);

				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameters target host, port
	if(argc-optind != 2)
	{
		fprintf (stderr, "Wrong arguments, try --help.\n\n");
		exit(1);
	}

	sendToHost=argv[optind];
	sendToPort=argv[++optind];

	//osc server
	lo_st = lo_server_thread_new(localPort, error);

	//destination address
	loa = lo_address_new(sendToHost, sendToPort);

	lo_server_thread_add_method(lo_st, "/accept", "", accept_handler, NULL);

	lo_server_thread_add_method(lo_st, "/deny", "fi", deny_handler, NULL);

	lo_server_thread_add_method(lo_st, "/pause", "", pause_handler, NULL);
	lo_server_thread_add_method(lo_st, "/trip", "itt", trip_handler, NULL);

	//create an array of input ports
	ioPortArray = (jack_port_t**) malloc(input_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	ioBufferArray = (jack_default_audio_sample_t**) malloc(input_port_count * sizeof(jack_default_audio_sample_t*));

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

	read_jack_properties();
	print_common_jack_properties();

	fprintf(stderr, "channels (capture): %d\n",input_port_count);

	fprintf(stderr,"multi-channel period size: %d bytes\n",
		input_port_count*period_size*bytes_per_sample
	);

	fprintf(stderr, "message rate: %.1f messages/s\n",
		(float)sample_rate/(float)period_size
	);

	//message size in bytes, including all metadata and blobs
	msg_size=message_size();

	//size in bytes totally used to send over network
	//experimental, compared with iptraf, wireshark
	//handshake (offer/accept/deny/pause) not counted

	//2, 44100, 512 -> msg size 4148 bytes ***

	//http://ask.wireshark.org/questions/9982/mtu-size-and-path-mtu-discovery

	//on the wire:
	//sequence of
	//proto IPv4 length 1514 Fragmented IP protocol UDP [Reassembled in #2140]
	//
	//	14 bytes ethernet II header
	//	20 bytes IP v4 header (length: 1500)
	//	1480 bytes data (payload)

	//proto IPv4 ...
	//UDP        length 1230 (Frame#2140)
	//	14 bytes ethernet II header
	//	20 bytes IP v4 header	
	//	8 bytes UDP header (length: 4156)
	//	4148 bytes data *** (reassembled)

	//per sent packet (!=message) (respect MTU):

	//how to calculate:
	//message_size / MTU payload
	//4148 bytes / 1480 bytes = 2.8027
	//floor(2.0827) = 2
	//2 x 1514 + 4148 mod 1480 
	//= 3028 + 1188 = 4216
	//+ 14 + 20 + 8 = 4258 total transfer

	transfer_size=floor(msg_size / 1480) * 1514
			+fmod(msg_size,1480)
			+14+20+8;

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

	fprintf(stderr, "expected network data rate: %.1f kbit/s (%.2f mb/s)\n\n",
		expected_network_data_rate,
		(float)expected_network_data_rate/1000/8
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
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if (ioPortArray[port] == NULL) {
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
		//exit (1);
	}
	
	int connection_port_count=fmin(input_port_count,sizeof(ports));

	if(autoconnect==1)
	{
		int i;
		for(i=0;i<connection_port_count;i++)
		{
			if (ports[i]!=NULL && jack_connect (client, ports[i],jack_port_name(ioPortArray[i]))) 
			{
				fprintf (stderr, "autoconnect: failed: %s -> %s\n",
					ports[i],jack_port_name(ioPortArray[i])
				);
			}
			else if(ports[i]!=NULL)
			{
				fprintf (stderr, "autoconnect: %s -> %s\n",
					ports[i],jack_port_name(ioPortArray[i])
				);
			}
		}
	}

	free (ports);

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	//start the osc server
	lo_server_thread_start(lo_st);

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
		sleep (1);
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

	if(nopause==0)
	{
		process_enabled=0;
		shutdown_in_progress=1;

		fprintf(stderr,"\nreceiver did not accept audio\nincompatible jack settings or format version on receiver:\nformat version: %.2f\nSR: %d\nshutting down... (see option --nopause)\n",argv[0]->f,argv[1]->i);
	}

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

	if(nopause==0)
	{
		fprintf(stderr,"\nreceiver requested pause\n");
		receiver_accepted=-1;
		msg_sequence_number=1;
	}
	else
	{
		//fprintf(stderr,"\nsender is configured to ignore\n");
	}

	return 0;
}
