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
//#include <jack/jack.h>//weak
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>
#include <unistd.h>

#include "jack_audio_common.h"
#include "jack_audio_send.h"

//tb/130427/131206/131211/140523
//gcc -o jack_audio_send jack_audio_send.c `pkg-config --cflags --libs jack liblo`

//inspired by several examples
//jack example clients simple_client.c, capture_client.c
//liblo example clients
//http://www.labbookpages.co.uk/audio/files/saffireLinux/inOut.c
//http://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html

int input_port_count=2; //param

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
//int max_transfer_size=32000;
//int max_transfer_size=65535;
int max_transfer_size=LO_MAX_MSG_SIZE;

//expected kbit/s that will arrive on receiver
float expected_network_data_rate=0;

//0: receiver denied 1: receiver accepted
int receiver_accepted=-1;

struct timeval tv;

int drop_every_nth_message=0; //param
int drop_counter=0;

int lo_proto=LO_UDP;

//osc
const char *localPort=NULL; //param
const char *sendToHost=NULL; //param
const char *sendToPort=NULL;; //param

//================================================================
int main(int argc, char *argv[])
{
	//jack
	const char **ports;
	//jack_options_t options = JackNullOption;
	jack_status_t status;

	//static struct option long_options[] in .h

	if(argc-optind<1)
	{
		print_header("jack_audio_send");
		fprintf(stderr, "Missing arguments, see --help.\n\n");
		exit(1);
	}

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
				print_header("jack_audio_receive");
				print_help();
				break;

			case 'b':
				print_version();
				break;

			case 'c':
				print_header("jack_audio_receive");
				check_lo_props(1);
				return 0;

			case 'd':
				localPort=optarg;
				break;

			case 'e':
				input_port_count=atoi(optarg);

				if(input_port_count>max_channel_count)
				{
					input_port_count=max_channel_count;
				}	
				break;

			case 'f':
				bytes_per_sample=2;
				break;

			case 'g':
				client_name=optarg;
				break;

			case 'h':
				server_name=optarg;
				jack_opts |= JackServerName;
				break;

			case 'i':
				update_display_every_nth_cycle=fmax(1,(uint64_t)atoll(optarg));
				break;

			case 'j':
				send_max=fmax(1,(uint64_t)atoll(optarg));
				test_mode=1;
				break;

			case 'k':
				io_host=optarg;
				break;

			case 'l':
				io_port=optarg;
				break;

			case 'm':
				drop_every_nth_message=atoi(optarg);
				break;

			case '?': //invalid commands
				//getopt_long already printed an error message
				print_header("jack_audio_send");
				fprintf(stderr, "Wrong arguments, see --help.\n\n");
				exit(1);
				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameters must be target host, port
	if(argc-optind!=2)
	{
		print_header("jack_audio_send");
		fprintf(stderr, "Wrong arguments, see --help.\n\n");
		exit(1);
	}

	sendToHost=argv[optind];
	sendToPort=argv[++optind];

	//destination address
	loa=lo_address_new_with_proto(lo_proto, sendToHost, sendToPort);

	//for commuication with a gui / other controller / visualizer
	loio=lo_address_new_with_proto(LO_UDP, io_host, io_port);

	//if was not set with option --lport
	if(localPort==NULL)
	{
		localPort="9990";
	}

	//if was set to use random port
	else if(atoi(localPort)==0)
	{
		//for lo_server_thread_new_with_proto
		localPort=NULL;
	}

	//add osc hooks & start osc server early (~right after cmdline parsing)
	registerOSCMessagePatterns(localPort);

	lo_server_thread_start(lo_st);

	//read back port (in case of random)
	//could use 
	//int lo_server_thread_get_port (lo_server_thread st)
	const char *osc_server_url=lo_server_get_url(lo_server_thread_get_server(lo_st));
	localPort=lo_url_get_port(osc_server_url);
	//int lport=lo_server_thread_get_port(lo_st);

	//notify osc gui
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_float(msgio, version);
		lo_message_add_float(msgio, format_version);
		lo_send_message(loio, "/startup", msgio);
		lo_message_free(msgio);
	}

	//
	if(check_lo_props(0)>0)
	{
		return 1;
	}

	if(shutup==0)
	{
		print_header("jack_audio_send");

		if(input_port_count>max_channel_count)
		{
			fprintf(stderr,"/!\\ limiting capture ports to %d, sry\n",max_channel_count);

		}
		if(test_mode==1)
		{
			fprintf(stderr,"/!\\ limiting number of messages: %" PRId64 "\n",send_max);
		}
	}

	//check for default jack server env var
	char *evar=getenv("JACK_DEFAULT_SERVER");
	if(evar==NULL || strlen(evar)<1)
	{
#ifndef _WIN
		unsetenv("JACK_DEFAULT_SERVER");
#endif
	}
	else if(server_name==NULL)
	{
		//use env var if no server was given with --sname
		server_name=evar;
	}

	if(server_name==NULL || strlen(server_name)<1)
	{
		server_name="default";
	}

	if(client_name==NULL)
	{
		client_name="send";
	}

	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?). this is fatal: jack_audio_send needs JACK to run.\n");
		io_quit("nolibjack");
		exit(1);
	}

	//create an array of input ports
	ioPortArray=(jack_port_t**) malloc(input_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	ioBufferArray=(jack_default_audio_sample_t**) malloc(input_port_count * sizeof(jack_default_audio_sample_t*));

	//open a client connection to the JACK server
	client=jack_client_open2(client_name, jack_opts, &status, server_name);
	if(client==NULL) 
	{
		fprintf(stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		if(status & JackServerFailed) 
		{
			fprintf(stderr, "Unable to connect to JACK server\n");
			io_quit("nojack");
		}
		exit(1);
	}

	if(shutup==0)
	{
		fprintf(stderr,"sending from UDP port: %s\n",localPort);
		fprintf(stderr,"target host:port: %s:%s\n",sendToHost,sendToPort);
	}

	client_name=jack_get_client_name(client);

	if(shutup==0)
	{
		fprintf(stderr,"started JACK client '%s' on server '%s'\n",client_name,server_name);
		if(status & JackNameNotUnique) 
		{
			fprintf(stderr, "/!\\ name '%s' was automatically assigned\n", client_name);
		}
	}

	if(status & JackNameNotUnique) 
	{
		io_simple_string("/client_name_changed",client_name);
	}

	read_jack_properties();

	if(shutup==0) //print even if quiet
	{
		print_common_jack_properties();

		fprintf(stderr, "channels (capture): %d\n",input_port_count);

		print_bytes_per_sample();

		if(nopause==1)
		{
			fprintf(stderr, "immediate send, ignore /pause and /deny: yes\n");
		}
		else
		{
			fprintf(stderr, "immediate send, no pause or shutdown: no\n");
		}

		if(drop_every_nth_message>0)
		{
			fprintf(stderr, "artificial message drops: every %d\n",drop_every_nth_message);
		}

		fprintf(stderr,"multi-channel period size: %d bytes\n",
			input_port_count*period_size*bytes_per_sample
		);

		fprintf(stderr, "message rate: %.1f messages/s\n",
			(float)sample_rate/(float)period_size
		);
	}//end if shutup==0

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
	//UDP	length 1230 (Frame#2140)
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

	if(shutup==0) //print even if quiet
	{
		fprintf(stderr,"message length: %d bytes\n", msg_size);
		fprintf(stderr,"transfer length: %d bytes (%.1f %% overhead)\n", 
			transfer_size,
			100-100*(float)input_port_count*period_size*bytes_per_sample/(float)transfer_size
		);


		if(transfer_size>=LO_MAX_MSG_SIZE)
		{
			fprintf(stderr,"/!\\ receiver(s) must support message size > %d\n",LO_MAX_MSG_SIZE);
		}
	}
	if(transfer_size>max_transfer_size)
	{
		fprintf(stderr,"sry, can't do. max transfer length: %d. reduce input channel count or use 16 bit.\n",max_transfer_size);
		io_quit("transfer_size_too_large");

		exit(1);
		//signal_handler(42);
	}

	expected_network_data_rate=(float)sample_rate/(float)period_size 
		* transfer_size
		* 8 / 1000;

	if(shutup==0) //print even if quiet
	{
		fprintf(stderr, "expected network data rate: %.1f kbit/s (%.2f MB/s)\n",
			expected_network_data_rate,
			(float)expected_network_data_rate/1000/8
		);

		//when near to 100 / 1000 mbit limit
		if(expected_network_data_rate/1000/8 > 12)
		{
			fprintf(stderr,"/!\\ high data rate (OK on localhost), use GigE\n");
		}

		fprintf(stderr,"\n");
	}

	io_dump_config();

	//JACK will call process() for every cycle (given by JACK)
	//NULL could be config/data struct
	jack_set_process_callback(client, process, NULL);

	jack_set_xrun_callback(client, xrun_handler, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);

	// Register each output port
	int port=0;
	for(port=0; port<input_port_count; port ++)
	{
		// Create port name
		char* portName;
		if(asprintf(&portName, "input_%d", (port+1))<0) 
		{
			fprintf(stderr, "Could not create portname for port %d\n", port);
			io_quit("port_error");
			exit(1);
		}

		// Register the input port
		ioPortArray[port]=jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if(ioPortArray[port]==NULL) 
		{
			fprintf(stderr, "Could not create input port %d\n", (port+1));
			io_quit("port_error");
			exit(1);
		}
	}

	//now activate client in JACK, starting with process() cycles
	if(jack_activate(client)) 
	{
		fprintf(stderr, "cannot activate client\n\n");
		io_quit("cannot_activate_client");
		exit(1);
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

	ports=jack_get_ports(client, NULL, pat,
				JackPortIsPhysical|JackPortIsOutput);
	if(ports==NULL) 
	{
		if(shutup==0)
		{
			fprintf(stderr, "no physical capture ports found\n");
		}
		//exit(1);
	}
	
	if(autoconnect==1)
	{
		int j=0;
		int i=0;
		for(i=0;i<input_port_count;i++)
		{

			if(ports[i]!=NULL 
				&& ioPortArray[j]!=NULL 
				&& jack_port_name(ioPortArray[j])!=NULL)
			{
				if(!jack_connect(client, ports[i],jack_port_name(ioPortArray[j])))
				{
					if(shutup==0)
					{
						fprintf(stderr, "autoconnect: %s -> %s\n",
							ports[i],jack_port_name(ioPortArray[j])
						);
					}
					io_simple_string_double("/autoconnect",ports[i],jack_port_name(ioPortArray[j]));
					j++;
				}
				else
				{
					if(shutup==0)
					{
						fprintf(stderr, "autoconnect: failed: %s -> %s\n",
							ports[i],jack_port_name(ioPortArray[j])
						);
					}
				}
			}
		}//end for all input ports

		if(shutup==0)
		{
			fprintf(stderr, "\n");
		}
	}

	free(ports);

	fflush(stderr);

	//signal handler callbacks to cleanly shutdown when program is about to exit
#ifndef _WIN
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
#endif
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	//start the osc server
	lo_server_thread_start(lo_st);

	process_enabled=1;

	io_simple("/start_main_loop");

	//run possibly forever until not interrupted by any means
	while(1) 
	{
		//try clean shutdown, mainly to avoid possible audible glitches	
		if(shutdown_in_progress==1)
		{
			signal_handler(42);
		}
#ifdef WIN_
		Sleep(1000);
#else
		sleep(1);
#endif
	}

	exit(0);
}//end main

//================================================================
int process(jack_nframes_t nframes, void *arg)
{
	//fprintf(stderr,".");
	//return 0;

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

				if(shutup==0 && quiet==0)
				{
					//print info "in-place" with \r
					fprintf(stderr,"\r# %" PRId64 " offering audio to %s:%s...",
						msg_sequence_number,
						lo_address_get_hostname(loa),
						lo_address_get_port(loa)
					);
					fflush(stderr);
				}

				if(io_())
				{
					lo_message msgio=lo_message_new();
					lo_message_add_int32(msgio,msg_sequence_number);
					lo_send_message(loio, "/offering", msgio);
					lo_message_free(msgio);
				}

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
		2) h: xrun counter (sender side, as all the following meta data)
		3) t: timetag (seconds since Jan 1st 1900 in the UTC, fraction 1/2^32nds of a second)
		4) i: sampling rate
		5) b: blob of channel 1 (period size * bytes per sample) bytes long
		...
		...) b: up to n channels
*/
		lo_message msg=lo_message_new();

		//add message counter
		lo_message_add_int64(msg,msg_sequence_number);
		//indicate how many xruns on sender
		lo_message_add_int64(msg,local_xrun_counter);

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
		for(i=0; i<input_port_count; i++)
		{
			jack_default_audio_sample_t *o1;

			//get "the" buffer
			o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);

			//32 bit float
			if(bytes_per_sample==4)
			{
				//fill blob from buffer
				blob[i]=lo_blob_new(bytes_per_sample*nframes,o1);
			}
			//16 bit pcm
			else
			{
				int16_t o1_16[nframes];

				int w;
				for(w=0;w<nframes;w++)
				{
					o1_16[w]=o1[w]*32760;
				}

				//fill blob from buffer
				blob[i]=lo_blob_new(bytes_per_sample*nframes,o1_16);
			}

			lo_message_add_blob(msg,blob[i]);		
		}

		//drop messages for test purposes
		if(drop_every_nth_message>0)
		{
			drop_counter++;

			if(drop_counter>=drop_every_nth_message)
			{
				drop_counter=0;
			}
			else
			{
				lo_send_message(loa, "/audio", msg);
			}
		}
		else
		{
			//==================================
			lo_send_message(loa, "/audio", msg);
		}

		//fprintf(stderr,"msg size %zu\n",lo_message_length(msg,"/audio"));

		//free resources to keep memory clean
		lo_message_free(msg);
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

			char *units="MB";
			float total_size_transferred_mb=(float)(transfer_size*msg_sequence_number)/1000/1000;
			float total_size_transferred=total_size_transferred_mb;
			//if > 10 gig
			if(total_size_transferred_mb>=10000)
			{
				total_size_transferred=total_size_transferred_mb/1000;
				units="GB";
			}

			if(shutup==0 && quiet==0)
			{
				//print info "in-place" with \r
				fprintf(stderr,"\r# %" PRId64 
					" (%s) xruns: %" PRId64 " tx: %" PRId64 " bytes (%.2f %s) p: %.1f%s",
					msg_sequence_number,
					hms,
					local_xrun_counter,
					transfer_size*msg_sequence_number,/*+140, //140: minimal offer/accept*/
					/*(float)(transfer_size*msg_sequence_number)/1000/1000,+140)/1000/1000*/
					total_size_transferred,
					units,
					(float)frames_since_cycle_start_avg/(float)period_size,
					"\033[0J"
				);
				fflush(stderr);
			}
			if(io_())
			{
				//=======
				lo_message msgio=lo_message_new();
				lo_message_add_int64(msgio, msg_sequence_number);//0
				lo_message_add_string(msgio, hms);		//1
				lo_message_add_int64(msgio, local_xrun_counter); //2
				lo_message_add_int64(msgio, transfer_size*msg_sequence_number); //3
				lo_message_add_float(msgio, total_size_transferred); //4
				lo_message_add_string(msgio, units); 		//5
				lo_message_add_float(msgio,			//6
					(float)frames_since_cycle_start_avg/(float)period_size);

				lo_send_message(loio, "/sending", msgio);
				lo_message_free(msgio);
			}//end if io_

			relaxed_display_counter=0;
		}
		relaxed_display_counter++;

		msg_sequence_number++;

	} //end process enabled

	if(last_test_cycle==1)
	{
		if(shutup==0)
		{
			fprintf(stderr,"\ntest finished after %" PRId64 " messages\n",msg_sequence_number-1);
			fprintf(stderr,"(waiting and buffering messages not included)\n");
		}

		io_simple_long("/test_finished",msg_sequence_number-1);

		shutdown_in_progress=1;
	}

	//simulate long cycle process duration
	//usleep(1000);

	frames_since_cycle_start=jack_frames_since_cycle_start(client);

	return 0;
} //end process()

//====================================================
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

	receiver should answer with /accept or /deny
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

	lo_send_message(loa, "/offer", msg);

	//free resources to keep memory clean
	lo_message_free(msg);
}//end offer_audio_to_receiver

//==========================================================
void registerOSCMessagePatterns(const char *port)
{
	//LO_MAX_UDP_MSG_SIZE & LO_DEFAULT_MAX_MSG_SIZE are now defined
	max_transfer_size=MIN_(LO_MAX_UDP_MSG_SIZE,LO_DEFAULT_MAX_MSG_SIZE);

	//osc server
	//lo_st=lo_server_thread_new(localPort, error);
	lo_st=lo_server_thread_new_with_proto(port, lo_proto, osc_error_handler);

	lo_server_thread_add_method(lo_st, "/accept", "", osc_accept_handler, NULL);
	lo_server_thread_add_method(lo_st, "/deny", "fii", osc_deny_handler, NULL);
	lo_server_thread_add_method(lo_st, "/pause", "", osc_pause_handler, NULL);
	lo_server_thread_add_method(lo_st, "/quit", "", osc_quit_handler, NULL);

}//

//================================================================
// /accept
int osc_accept_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}
	io_simple("/receiver_accepted_transmission");
	receiver_accepted=1;
	msg_sequence_number=1;
	return 0;
}

//================================================================
// /deny
int osc_deny_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	float format_version=argv[0]->f;
	int sample_rate=argv[1]->i;
	int bytes_per_sample=argv[2]->i;

	if(nopause==0)
	{
		process_enabled=0;
		shutdown_in_progress=1;
		if(shutup==0)
		{
			fprintf(stderr,"\nreceiver did not accept audio\nincompatible JACK settings or format version on receiver:\nformat version: %.2f\nSR: %d bytes per sample: %d\nshutting down... (see option --nopause)\n", format_version, sample_rate, bytes_per_sample);
		}
	}

	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_float(msgio, format_version);
		lo_message_add_int32(msgio, sample_rate);
		lo_message_add_int32(msgio, bytes_per_sample);
		lo_send_message(loio, "/receiver_denied_transmission", msgio);
		lo_message_free(msgio);
	}

	fflush(stderr);

	return 0;
}

//================================================================
// /pause
int osc_pause_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1)
	{
		return 0;
	}

	if(nopause==0)
	{
		if(shutup==0)
		{
			fprintf(stderr,"\nreceiver requested pause\n");
		}

		io_simple("/receiver_requested_pause");
		receiver_accepted=-1;
		msg_sequence_number=1;
	}
	else
	{
		//fprintf(stderr,"\nsender is configured to ignore\n");
	}

	fflush(stderr);

	return 0;
}

//================================================================
// /quit
int osc_quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
//	io_quit("quit_received");
	signal_handler(42);
	return 0;
}

//================================================================
void osc_error_handler(int num, const char *msg, const char *path)
{
	fprintf(stderr,"\nliblo server error %d: %s\n", num, msg);
	io_quit("liblo_error_xxx");

	exit(1);
}

//================================================================
void signal_handler(int sig)
{
	fprintf(stderr, "\nterminate signal %d received. cleaning up...",sig);

	io_quit("terminated");

	shutdown_in_progress=1;

	jack_client_close(client);
	lo_server_thread_free(lo_st);

	fprintf(stderr,"done\n");

	exit(0);
}

//================================================================
void io_dump_config()
{
	if(io_())
	{
		//similar order to startup output
		//dont send easy to calculate fields (calc in gui)

		lo_message msgio=lo_message_new();

		//basic setup of a->b (localhost:port -> targethost:port)
		lo_message_add_int32(msgio,atoi(localPort));	//0
		lo_message_add_string(msgio,sendToHost);	//1
		lo_message_add_int32(msgio,atoi(sendToPort));	//2

		//local jack info
		lo_message_add_string(msgio,client_name);	//3
		lo_message_add_string(msgio,server_name);	//4

		lo_message_add_int32(msgio,sample_rate);	//5
		lo_message_add_int32(msgio,period_size);	//6

		lo_message_add_int32(msgio,input_port_count);	//7

		//transmission info
		//this is not a property of local JACK
		lo_message_add_int32(msgio,bytes_per_sample);	//8
		lo_message_add_int32(msgio,nopause);		//9
		//multi-channel period size
		//lo_message_add_int32(msgio,input_port_count*period_size*bytes_per_sample);
		//message rate
		//lo_message_add_float(msgio,(float)sample_rate/(float)period_size);
		lo_message_add_int32(msgio,msg_size);		//10
		lo_message_add_int32(msgio,transfer_size);	//11
		//overhead (0-1 = 0 - 100%)
		//lo_message_add_float(msgio,(float)input_port_count*period_size*bytes_per_sample/(float)transfer_size);

		lo_message_add_float(msgio,expected_network_data_rate);	//12
		//lo_message_add_float(msgio,);

		lo_message_add_int32(msgio,test_mode);			//13
		lo_message_add_int32(msgio,send_max);			//14
		lo_message_add_int32(msgio,drop_every_nth_message);	//15

//should be global
//		lo_message_add_int32(msgio,max_buffer_size);
//		lo_message_add_int32(msgio,rb_size);

		lo_send_message(loio, "/config_dump", msgio);
		lo_message_free(msgio);
	}
}//end io_dump_config

//================================================================
int message_size()
{
	lo_message msg=lo_message_new();
	lo_message_add_int64(msg,msg_sequence_number);
	lo_message_add_int64(msg,local_xrun_counter);

	gettimeofday(&tv, NULL);
	lo_timetag tt;
	tt.sec=tv.tv_sec;
	tt.frac=tv.tv_usec;
	lo_message_add_timetag(msg,tt);
	lo_message_add_int32(msg,1111);

	lo_blob blob[input_port_count];
	void* membuf=malloc(period_size*bytes_per_sample);

	int i;
	for(i=0; i<input_port_count; i++)
	{
		blob[i]=lo_blob_new(period_size*bytes_per_sample,membuf);
		lo_message_add_blob(msg,blob[i]);		
	}

	int msg_size=lo_message_length(msg,"/audio");

	//free resources to keep memory clean
	lo_message_free(msg);
	free(membuf);

	for(i=0;i<input_port_count;i++)
	{
		lo_blob_free(blob[i]);
	}

	return msg_size;
}//end message_size
