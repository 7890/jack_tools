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
//#include <jack/jack.h>//weak
//#include <jack/ringbuffer.h>//weak
#include <lo/lo.h>
#include <sys/time.h>
#include <getopt.h>
#include <unistd.h>

#include "jack_audio_common.h"
#include "jack_audio_receive.h"

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
uint64_t pre_buffer_size=4; //param
uint64_t pre_buffer_counter=0;

//if no parameter given, buffer size
//is calculated later on
//periods
uint64_t max_buffer_size=0; //param

//defined by sender
uint64_t message_number=0;

//to detect gaps
uint64_t message_number_prev=0;

//count what we process
uint64_t process_cycle_counter=0;

//local xruns since program start of receiver
//uint64_t local_xrun_counter=0;

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

/*
int close_on_incomp=0; //param
int rebuffer_on_restart=0; //param
int rebuffer_on_underflow=0; //param
int channel_offset=0; //param
*/

int use_tcp=0; //param
int lo_proto=LO_UDP;

char* remote_tcp_server_port;

int not_yet_ready=1;

//osc
const char *localPort=NULL;

//================================================================
int main(int argc, char *argv[])
{
	//jack
	const char **ports;
	//jack_options_t options = JackNullOption;
	jack_status_t status;

//options struct was here

	if(argc-optind<1)
	{
		print_header("jack_audio_receive");
		fprintf(stderr, "Missing arguments, see --help.\n\n");
		exit(1);
	}

	int opt;
 	//do until command line options parsed
	while(1)
	{
		/* getopt_long stores the option index here. */
		int option_index=0;

		opt=getopt_long(argc, argv, "", long_options, &option_index);

		/* Detect the end of the options. */
		if(opt==-1)
		{
			break;
		}
		switch(opt)
		{
			case 0:

			 /* If this option set a flag, do nothing else now. */
			if(long_options[option_index].flag!=0)
			{
				break;
			}

			case 'h':
				print_header("jack_audio_receive");
				print_help();
				break;

			case 'v':
				print_version();
				break;

			case 'x':
				print_header("jack_audio_receive");
				check_lo_props(1);
				return 1;

			case 'o':
				output_port_count=atoi(optarg);

				if(output_port_count>max_channel_count)
				{
					output_port_count=max_channel_count;
				}
				port_count=fmin(input_port_count,output_port_count);
				break;

			case 'f':
				channel_offset=atoi(optarg);
				break;

			case 'y':
				bytes_per_sample=2;
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
				break;

			case 'a':
				io_host=optarg;
				break;

			case 'c':
				io_port=optarg;
				break;

			case 't':
				use_tcp=1;
				remote_tcp_server_port=optarg;
				break;

			case '?': //invalid commands
				/* getopt_long already printed an error message. */
				print_header("jack_audio_receive");
				fprintf(stderr, "Wrong arguments, see --help.\n\n");
				exit(1);

				break;
 	 
			default:
				break;
		 } //end switch op
	}//end while(1)


	//remaining non optional parameters listening port
	if(argc-optind!=1)
	{
		print_header("jack_audio_receive");
		fprintf(stderr, "Wrong arguments, see --help.\n\n");
		exit(1);
	}

	localPort=argv[optind];

	//for commuication with a gui / other controller / visualizer
	loio=lo_address_new_with_proto(LO_UDP, io_host, io_port);

	//if was set to use random port
	if(atoi(localPort)==0)
	{
		//for lo_server_thread_new_with_proto
		localPort=NULL;
	}

	//add osc hooks & start osc server early (~right after cmdline parsing)
	registerOSCMessagePatterns(localPort);

	lo_server_thread_start(lo_st);

	//read back port (in case of random)
	//could use 
	//int lo_server_thread_get_port(lo_server_thread st)
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

	if(check_lo_props(0)>0)
	{
		return 1;
	}


	if(use_tcp==1)
	{
		lo_proto=LO_TCP;
	}

	if(shutup==0)
	{
		print_header("jack_audio_receive");

		if(output_port_count>max_channel_count)
		{
			fprintf(stderr,"/!\\ limiting playback ports to %d, sry\n",max_channel_count);
		}

		if(test_mode==1)
		{
			fprintf(stderr,"/!\\ limiting number of messages: %" PRId64 "\n",receive_max);
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

	if(server_name==NULL || strlen(server_name)<=0)
	{
		server_name="default";
	}

	if(client_name==NULL)
	{
		client_name="receive";
	}

	if(have_libjack()!=0)
	{
		fprintf(stderr,"/!\\ libjack not found (JACK not installed?). this is fatal: jack_audio_receive needs JACK to run.\n");
		io_quit("nolibjack");
		exit(1);
	}

	//initialize time
	gettimeofday(&tv, NULL);
	tt_prev.sec=tv.tv_sec;
	tt_prev.frac=tv.tv_usec;

	//create an array of input ports
	ioPortArray=(jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//create an array of audio sample pointers
	//each pointer points to the start of an audio buffer, one for each capture channel
	ioBufferArray=(jack_default_audio_sample_t**) malloc(output_port_count * sizeof(jack_default_audio_sample_t*));

	//open a client connection to the JACK server
	client=jack_client_open(client_name, jack_opts, &status, server_name);
	if(client==NULL) 
		{
		fprintf(stderr,"jack_client_open() failed, status = 0x%2.0x\n", status);
		if(status & JackServerFailed) 
		{
			fprintf(stderr,"Unable to connect to JACK server.\n");
			io_quit("nojack");
		}
		exit(1);
	}

	if(use_tcp==1)
	{

		if(shutup==0)
		{
			fprintf(stderr,"receiving on TCP port: %s\n",localPort);
		}
	}
	else
	{
		if(shutup==0)
		{
			fprintf(stderr,"receiving on UDP port: %s\n",localPort);
	}
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
		io_simple("/client_name_changed");
	}

	//print startup info

	read_jack_properties();

	if(shutup==0)
	{
		print_common_jack_properties();

		fprintf(stderr,"channels (playback): %d\n",output_port_count);
		fprintf(stderr,"channel offset: %d\n",channel_offset);

		print_bytes_per_sample();

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

	}//end cond. print

	char buf[64];
	format_seconds(buf,(float)pre_buffer_size*period_size/(float)sample_rate);

	uint64_t rb_size_pre=pre_buffer_size*output_port_count*period_size*bytes_per_sample;

	if(shutup==0)
	{
		fprintf(stderr,"initial buffer size: %" PRId64 " mc periods (%s, %" PRId64 " bytes, %.2f MB)\n",
			pre_buffer_size,
			buf,
			rb_size_pre,
			(float)rb_size_pre/1000/1000
		);
	}
	buf[0]='\0';

	//ringbuffer size bytes
	uint64_t rb_size;

	//ringbuffer mc periods
	int max_buffer_mc_periods;

	//max given as param (user knows best. if pre=max, overflows are likely)
	if(max_buffer_size>0)
	{
		max_buffer_mc_periods=fmax(pre_buffer_size,max_buffer_size);
		rb_size=max_buffer_mc_periods
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
	if(shutup==0)
	{
		fprintf(stderr,"allocated buffer size: %" PRId64 " mc periods (%s, %" PRId64 " bytes, %.2f MB)\n",
			max_buffer_size,
			buf,
			rb_size,
			(float)rb_size/1000/1000
		);
	}
	buf[0]='\0';

	io_dump_config();

	//====================================
	//main ringbuffer osc blobs -> jack output
	rb=jack_ringbuffer_create(rb_size);
	//helper ringbuffer: used when remote period size < local period size
	rb_helper=jack_ringbuffer_create(rb_size);

	if(rb==NULL)
	{
		fprintf(stderr,"could not create a ringbuffer with that size.\n");
		fprintf(stderr,"try --max <smaller size>.\n");
		io_quit("ringbuffer_too_large");
		exit(1);
	}

	//JACK will call process() for every cycle (given by JACK)
	//NULL could be config/data struct
	jack_set_process_callback(client, process, NULL);

	jack_set_xrun_callback(client, xrun_handler, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);

	// Register each output port
	int port;
	for(port=0; port<output_port_count; port ++)
	{
		// Create port name
		char* portName;
		if(asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr,"Could not create portname for port %d", port);
			io_quit("port_error");
			exit(1);
		}

		// Register the output port
		ioPortArray[port]=jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if(ioPortArray[port]==NULL) 
		{
			fprintf(stderr,"Could not create output port %d\n", (port+1));
			io_quit("port_error");
			exit(1);
		}
	}

	/* Tell the JACK server that we are ready to roll. Our
	 * process() callback will start running now. */
	if(jack_activate(client)) 
	{
		fprintf(stderr, "cannot activate client");
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

	ports=jack_get_ports(client, NULL, pat, JackPortIsPhysical|JackPortIsInput);

	if(ports==NULL) 
	{
		if(shutup==0)		
		{
			fprintf(stderr,"no physical playback ports\n");
		}
		//exit(1);
	}
	
	if(autoconnect==1)
	{
		fprintf(stderr, "\n");

		int j=0;
		int i;
		for(i=0;i<output_port_count;i++)
		{
			if(ports[i]!=NULL 
				&& ioPortArray[j]!=NULL 
				&& jack_port_name(ioPortArray[j])!=NULL)
			{
				if(!jack_connect(client, jack_port_name(ioPortArray[j]), ports[i]))
				{
					if(shutup==0)
					{
						fprintf(stderr, "autoconnect: %s -> %s\n",
							jack_port_name(ioPortArray[j]),ports[i]
						);
					}
					io_simple_string_double("/autoconnect",jack_port_name(ioPortArray[j]),ports[i]);
					j++;
				}
				else
				{
					if(shutup==0)
					{
						fprintf(stderr, "autoconnect: failed: %s -> %s\n",
							jack_port_name(ioPortArray[j]),ports[i]
						);
					}
				}
			}
			else
			{
				//no more playback ports
				break;
			}
		}//end for all output ports

		if(shutup==0)
		{
			fprintf(stderr, "\n");
		}
	}

	free(ports);

	fflush(stderr);

	/* install a signal handler to properly quits jack client */
#ifndef _WIN
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
#endif
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	if(use_tcp==1)
	{
		//10 MB max
		int desired_max_tcp_size=10000000;

		lo_server s=lo_server_thread_get_server(lo_st);
		int ret_set_size=lo_server_max_msg_size(s, desired_max_tcp_size);

		if(shutup==0)
		{
			printf("set tcp max size return: %d\n",ret_set_size);
			io_simple("/tcp_max_size_xxxx");
		}
	}

	not_yet_ready=0;

	io_simple("/start_main_loop");

	//run possibly forever until not interrupted by any means
	while(1) 
	{
		//possibly clean shutdown without any glitches
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

	//if shutting down fill buffers with 0 and return
	if(shutdown_in_progress==1)
	{
		int i;
		for(i=0; i < output_port_count; i++)
		{
			jack_default_audio_sample_t *o1;
			o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i], nframes);
			//memset(o1, 0, bytes_per_sample*nframes);
			//always 4 bytes, 32 bit float
			memset(o1, 0, 4*nframes);
		}
		return 0;
	}

	if(process_enabled==1)
	{
		//if no data for this cycle(all channels) 
		//is available(!), fill buffers with 0 or re-use old buffers and return
		if(jack_ringbuffer_read_space(rb) < port_count * bytes_per_sample*nframes)
		{
			int i;
			for(i=0; i < output_port_count; i++)
			{
				if(zero_on_underflow==1)
				{
					jack_default_audio_sample_t *o1;
					o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i], nframes);

					//memset(o1, 0, bytes_per_sample*nframes);
					//always 4 bytes, 32 bit float
					memset(o1, 0, 4*nframes);
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
		}//end not enough data available in ringbuffer

		process_cycle_counter++;

		if(process_cycle_counter>receive_max-1 && test_mode==1)
		{
			last_test_cycle=1;
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
		for(i=0; i<port_count; i++)
		{
			jack_default_audio_sample_t *o1;
			o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i], nframes);

			int16_t *o1_16;

			if(bytes_per_sample==2)
			{
				o1_16=malloc(bytes_per_sample*nframes);
			}

			//32 bit float
			if(bytes_per_sample==4)
			{
				jack_ringbuffer_read(rb, (char*)o1, bytes_per_sample*nframes);
			}
			//16 bit pcm
			else
			{
				jack_ringbuffer_read(rb, (char*)o1_16, bytes_per_sample*nframes);

				int x;
				for(x=0;x<nframes;x++)
				{
					o1[x]=(float)o1_16[x]/32760;
				}

				free(o1_16);
			}

			/*
			fprintf(stderr,"\rreceiving from %s:%s",
				sender_host,sender_port
			);
			*/

			print_info();

		}//end for i < port_count

		//requested via /buffer, for test purposes (make buffer "tight")
		if(requested_drop_count>0)
		{
			uint64_t drop_bytes_count=requested_drop_count
				*port_count*period_size*bytes_per_sample;

			jack_ringbuffer_read_advance(rb,drop_bytes_count);

			requested_drop_count=0;
			multi_channel_drop_counter=0;
		}
	}//end if process_enabled==1
	else //if process_enabled==0
	{
		int i;
		for(i=0; i<port_count; i++)
		{
			jack_default_audio_sample_t *o1;
			o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i], nframes);

			//this is per channel, not per cycle. *port_count
			if(relaxed_display_counter>=update_display_every_nth_cycle*port_count
				|| last_test_cycle==1
			)
			{
				//only for init
				if((int)message_number<=0 && starting_transmission==0)
				{
					if(shutup==0 && quiet==0)
					{
						fprintf(stderr,"\rwaiting for audio input data...");
					}
					io_simple("/wait_for_input");
				}
				else
				{
					if(shutup==0 && quiet==0)
					{
						fprintf(stderr,"\r# %" PRId64 " buffering... mc periods to go: %" PRId64 "%s",
							message_number,
							pre_buffer_size-pre_buffer_counter,
							"\033[0J"
						);
					}

					if(io_())
					{
						lo_message msgio=lo_message_new();

						lo_message_add_int64(msgio,message_number);
						lo_message_add_int64(msgio,pre_buffer_size-pre_buffer_counter);

						lo_send_message(loio, "/buffering", msgio);
						lo_message_free(msgio);
					}
				}

				fflush(stderr);

				relaxed_display_counter=0;
			}
			relaxed_display_counter++;

			//set output buffer silent
			//memset(o1, 0, port_count*bytes_per_sample*nframes);
			//always 4 bytes, 32 bit float
			memset(o1, 0, port_count*4*nframes);
		}//end for i < port_count
	}//end process_enabled==0

	//tasks independent of process_enabled 0/1

	//if sender sends less channels than we have output channels, wee need to fill them with 0
	if(input_port_count < output_port_count)
	{
		int i;
		for(i=0;i < (output_port_count-input_port_count);i++)
		{
			//jack_default_audio_sample_t *o1;
			//o1=(jack_default_audio_sample_t*)
			/////?
			jack_port_get_buffer(ioPortArray[input_port_count+i], nframes);
		}
	}

	if(last_test_cycle==1)
	{
		if(shutup==0)
		{
			fprintf(stderr,"\ntest finished after %" PRId64 " process cycles\n",process_cycle_counter);
			fprintf(stderr,"(waiting and buffering cycles not included)\n");
		}

		io_simple_long("/test_finished",process_cycle_counter);

		shutdown_in_progress=1;
	}

	//simulate long cycle process duration
	//usleep(1000);

	frames_since_cycle_start=jack_frames_since_cycle_start(client);

	return 0;
} //end process()

//================================================================
void print_info()
{
	uint64_t can_read_count=jack_ringbuffer_read_space(rb);

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

		if(shutup==0 && quiet==0)
		{
			fprintf(stderr,"\r# %" PRId64 " i: %s%d f: %.1f b: %" PRId64 " s: %.4f i: %.2f r: %" PRId64 
				" l: %" PRId64 " d: %" PRId64 " o: %" PRId64 " p: %.1f%s",
				message_number,
				offset_string,
				input_port_count,
				(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count,
				can_read_count,
				(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate,
				time_interval_avg*1000,
				remote_xrun_counter,
				local_xrun_counter,
				multi_channel_drop_counter,
				buffer_overflow_counter,
				(float)frames_since_cycle_start_avg/(float)period_size,
				"\033[0J"
			);
		}

		if(io_())
		{

			lo_message msgio=lo_message_new();

//will nee other format than in32

			lo_message_add_int64(msgio,message_number);
			lo_message_add_int32(msgio,input_port_count);
			lo_message_add_int32(msgio,channel_offset);

			lo_message_add_float(msgio,
				(float)can_read_count/(float)bytes_per_sample/(float)period_size/(float)port_count
			);

			lo_message_add_int64(msgio,can_read_count);

			lo_message_add_float(msgio,
				(float)can_read_count/(float)port_count/(float)bytes_per_sample/(float)sample_rate
			);

			lo_message_add_float(msgio,time_interval_avg*1000);
			lo_message_add_int64(msgio,remote_xrun_counter);
			lo_message_add_int64(msgio,local_xrun_counter);
			lo_message_add_int64(msgio,multi_channel_drop_counter);
			lo_message_add_int64(msgio,buffer_overflow_counter);

			lo_message_add_float(msgio,
				(float)frames_since_cycle_start_avg/(float)period_size
			);

			lo_send_message(loio, "/status", msgio);
			lo_message_free(msgio);
		}

		fflush(stderr);

		relaxed_display_counter=0;
	}
	relaxed_display_counter++;
}//end print_info

//================================================================
void registerOSCMessagePatterns(const char *port)
{
	/* osc server */
	//lo_st=lo_server_thread_new(port, error);
	lo_st=lo_server_thread_new_with_proto(port, lo_proto, osc_error_handler);

//AUDIO RELATED=========================================

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

	lo_server_thread_add_method(lo_st, "/offer", "fiiiifh", osc_offer_handler, NULL);

/*
	experimental

	/buffer ii

	1) i: target fill value for available periods in ringbuffer (multi channel)
	2) i: max. target buffer size in mc periods
	drop or add periods
	this will introduce a hearable click on drops
	or pause playback for rebuffering depending on the current buffer fill
*/

	lo_server_thread_add_method(lo_st, "/buffer", "ii", osc_buffer_handler, NULL);

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

//the max possible channel count depends on JACK period size, SR, liblo version (fixmax), 16/32 bit, 100/1000 mbit/s network
/*
	//support 1-n blobs / channels per message
	lo_server_thread_add_method(lo_st, "/audio", "hhtib", audio_handler, NULL);
	lo_server_thread_add_method(lo_st, "/audio", "hhtibb", audio_handler, NULL);
..
*/
	v=0;
	for(v=0;v<max_channel_count;v++)
	{
		typetag_string[data_offset+v]='b';
		lo_server_thread_add_method(lo_st, "/audio", typetag_string, osc_audio_handler, NULL);
	}

//GUI I/O, CONTROL RELATED==============================

/*
	/quit
	close / release resources and shutdown program
*/
	lo_server_thread_add_method(lo_st, "/quit", "", osc_quit_handler, NULL);


}//end registerocsmessages

//================================================================
// /offer
//sender offers audio
int osc_offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1 || not_yet_ready==1)
	{
		return 0;
	}

	float offered_format_version=argv[0]->f;

	int offered_sample_rate=argv[1]->i;
	int offered_bytes_per_sample=argv[2]->i;
	int offered_period_size=argv[3]->i;

//unused here
//	int offered_channel_count=argv[4]->i;
//	float offered_data_rate=argv[5]->f;
//	uint64_t request_counter=argv[6]->h;

	lo_message msg=lo_message_new();

	//send back to host that offered audio
	//lo_address loa=lo_message_get_source(data);

	lo_address loa;

	if(use_tcp==1)
	{
		lo_address loa_=lo_message_get_source(data);
		loa=lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
	}
	else
	{
		loa=lo_message_get_source(data);
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
		lo_send_message(loa, "/accept", msg);

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
		lo_send_message(loa, "/deny", msg);

		fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings or format version on sender:\nformat version: %.2f\nSR: %d\nbytes per sample: %d\ntelling sender to stop.\n",
			lo_address_get_hostname(loa),lo_address_get_port(loa),offered_format_version,offered_sample_rate,offered_bytes_per_sample
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

	fflush(stderr);

	return 0;
} //end osc_offer_handler

//================================================================
// /audio
//handler for audio messages
int osc_audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1 || not_yet_ready==1)
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

	//total args count minus metadata args count = number of blobs
	input_port_count=argc-data_offset;

	//only process useful number of channels
	port_count=fmin(input_port_count,output_port_count);

	if(port_count < 1)
	{
		fprintf(stderr,"channel offset %d >= available input channels %d! (nothing to receive). shutting down...\n"
			,channel_offset
			,channel_offset+input_port_count);
		fflush(stderr);
		shutdown_in_progress=1;
		return 0;
	}

	//check sample rate and period size if sender (re)started or values not yet initialized (=no /offer received)
	if(message_number_prev>message_number || message_number==1 || remote_sample_rate==0 || remote_period_size==0 )
	{
		lo_address loa;

		if(use_tcp==1)
		{
			lo_address loa_=lo_message_get_source(data);
			loa=lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
		}
		else
		{
			loa=lo_message_get_source(data);
		}

		strcpy(sender_host,lo_address_get_hostname(loa));
		strcpy(sender_port,lo_address_get_port(loa));

		//option --rebuff
		if(rebuffer_on_restart==1)
		{
			uint64_t can_read_count=jack_ringbuffer_read_space(rb);
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
				lo_message msg=lo_message_new();


				lo_message_add_float(msg,format_version);
				lo_message_add_int32(msg,sample_rate);
// /deny as reply to /audio should be the same as for /deny as reply to /offer
//will need change of /audio (add format, bytes_per_sample)
///////
				lo_message_add_int32(msg,99);

				lo_send_message(loa, "/deny", msg);
				lo_message_free(msg);

				fprintf(stderr,"\ndenying transmission from %s:%s\n(incompatible JACK settings on sender: SR: %d). telling sender to stop.\n",
					lo_address_get_hostname(loa),lo_address_get_port(loa),remote_sample_rate
				);
				fflush(stderr);

				message_number=0;
				message_number_prev=0;
				remote_sample_rate=0;
				remote_period_size=0;
				//pre_buffer_counter=0;
			}
			else
			{
				lo_address loa;

				if(use_tcp==1)
				{
					lo_address loa_=lo_message_get_source(data);
					loa=lo_address_new_with_proto(lo_proto,lo_address_get_hostname(loa_),remote_tcp_server_port);
				}
				else
				{
					loa=lo_message_get_source(data);
				}

				fprintf(stderr,"\ndenying transmission from %s:%s\nincompatible JACK settings on sender: SR: %d.\nshutting down (see option --close)...\n",
					lo_address_get_hostname(loa),lo_address_get_port(loa),remote_sample_rate
				);
				fflush(stderr);

				shutdown_in_progress=1;
				return 0;
			}
		}

		remote_period_size=lo_blob_datasize((lo_blob)argv[0+data_offset])/bytes_per_sample;

		if(shutup==0 && quiet==0)
		{
			fprintf(stderr,"\nsender was (re)started. ");

			lo_address loa=lo_message_get_source(data);
			fprintf(stderr,"receiving from %s:%s\n",lo_address_get_hostname(loa),lo_address_get_port(loa));
		}

		io_simple("/sender_restarted");

		if(shutup==0 && quiet==0)
		{
			if(remote_period_size!=period_size)
			{
				fprintf(stderr,"sender period size: %d samples (%.3f x local)\n\n",remote_period_size,(float)remote_period_size/period_size);
			}
			else
			{
				fprintf(stderr,"equal sender and receiver period size\n\n");
			}

			fflush(stderr);
		}

	}//end if "no-offer init" was needed

	remote_xrun_counter=argv[1]->h;

	lo_timetag tt=argv[2]->t;

	double msg_time=tt.sec+(double)tt.frac/1000000;
	double msg_time_prev=tt_prev.sec+(double)tt_prev.frac/1000000;
//unused for now
//	double time_now=tv.tv_sec+(double)tv.tv_usec/1000000;

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

	if(pre_buffer_counter>=pre_buffer_size && process_enabled==0)
	{
		//if buffer filled, start to output audio in process()
		process_enabled=1;
	}

	int mc_period_bytes=period_size*bytes_per_sample*port_count;

	//check if a whole mc period can be written to the ringbuffer
	uint64_t can_write_count=jack_ringbuffer_write_space(rb);
	if(can_write_count < mc_period_bytes)
	{
			buffer_overflow_counter++;
			/////////////////
			if(shutup==0 && quiet==0)
			{
				fprintf(stderr,"\rBUFFER OVERFLOW! this is bad -----%s","\033[0J");
			}
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
			unsigned char *data=lo_blob_dataptr((lo_blob)argv[i+data_offset]);
			//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

			//write to ringbuffer
			//==========================================
			//int cnt=
			jack_ringbuffer_write(rb, (void *) data, 
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
			unsigned char *data=lo_blob_dataptr((lo_blob)argv[i+data_offset]);
			//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

			//write to temporary ringbuffer until there is enough data
			//==========================================
			//int cnt=
			jack_ringbuffer_write(rb_helper, (void *) data, 
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
					//int w=
					jack_ringbuffer_write(rb,(void *)data,remote_period_size*bytes_per_sample);
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
				unsigned char *data=lo_blob_dataptr((lo_blob)argv[i+data_offset]);
				//fprintf(stderr,"size %d\n",lo_blob_datasize((lo_blob)argv[i+data_offset]));

				//write to ringbuffer
				//==========================================
				data+=k*period_size*bytes_per_sample;

				//int cnt=
				jack_ringbuffer_write(rb, (void *) data, 
					period_size*bytes_per_sample);
			}
			pre_buffer_counter++;
		}
	}

	return 0;
}//end osc_audio_handler

//================================================================
// /buffer
int osc_buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	if(shutdown_in_progress==1 || not_yet_ready==1)
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

	uint64_t rb_size=max_buffer_periods
		*output_port_count*bytes_per_sample*period_size;

	//create new buffer if not equal to current max
	//the current buffer will be lost
	if(max_buffer_periods!=max_buffer_size)
	{
		char buf[64];
		format_seconds(buf,(float)max_buffer_periods*period_size/(float)sample_rate);

		fprintf(stderr,"new ringbuffer size: %d mc periods (%s, %" PRId64 " bytes, %.2f MB)\n",
			max_buffer_periods,
			buf,
			rb_size,
			(float)rb_size/1000/1000
		);

		max_buffer_size=max_buffer_periods;

		rb=jack_ringbuffer_create(rb_size);
		// /buffer is experimental, it can segfault
	}

	//current size
	uint64_t can_read_count=jack_ringbuffer_read_space(rb);
	uint64_t can_read_periods_count=can_read_count/port_count/period_size/bytes_per_sample;

	if(pre_buffer_periods>can_read_periods_count)
	{
		//fill buffer
		uint64_t fill_periods_count=pre_buffer_periods-can_read_periods_count;
		fprintf(stderr,"-> FILL %" PRId64 "\n",fill_periods_count);

		pre_buffer_size=fill_periods_count;
		pre_buffer_counter=0;
		process_enabled=0;
	}
	else if(pre_buffer_periods<can_read_periods_count)
	{
		//do in process() (reader)
		requested_drop_count+=can_read_periods_count-pre_buffer_periods;
		fprintf(stderr," -> DROP %" PRId64 "\n",requested_drop_count);
	}
	
	fflush(stderr);
	return 0;
}//end osc_buffer_handler

//================================================================
// /quit
int osc_quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	signal_handler(42);	
	return 0;
}

//================================================================
void osc_error_handler(int num, const char *msg, const char *path)
{
	if(close_on_incomp==1 && shutdown_in_progress==0)
	{
		fprintf(stderr,"/!\\ liblo server error %d: %s %s\n", num, path, msg);

		fprintf(stderr,"telling sender to pause.\n");

		//lo_address loa=lo_address_new(sender_host,sender_port);
		lo_address loa=lo_address_new_with_proto(lo_proto, sender_host,sender_port);

		lo_message msg=lo_message_new();
		lo_send_message(loa, "/pause", msg);
		lo_message_free(msg);

		io_quit("incompatible_jack_settings");

		fprintf(stderr,"cleaning up...");

		jack_client_close(client);
		//lo_server_thread_free(lo_st);
		jack_ringbuffer_free(rb);
		jack_ringbuffer_free(rb_helper);
		fprintf(stderr,"done.\n");

		exit(1);
	}
	else if(shutdown_in_progress==0)
	{
		fprintf(stderr,"\r/!\\ liblo server error %d: %s %s\n", num, path, msg);
		//should be a param
	}
}//end osc_error_handler

//================================================================
//ctrl+c etc
static void signal_handler(int sig)
{
	shutdown_in_progress=1;
	process_enabled=0;

	fprintf(stderr,"\nterminate signal %d received.\n",sig);

	if(close_on_incomp==0)
	{
		fprintf(stderr,"telling sender to pause.\n");

		//lo_address loa=lo_address_new(sender_host,sender_port);
		lo_address loa=lo_address_new_with_proto(lo_proto, sender_host,sender_port);

		lo_message msg=lo_message_new();
		lo_send_message(loa, "/pause", msg);
		lo_message_free(msg);
	}

	io_quit("terminated");

	usleep(1000);

	fprintf(stderr,"cleaning up...");

	jack_client_close(client);
	lo_server_thread_free(lo_st);
	jack_ringbuffer_free(rb);
	jack_ringbuffer_free(rb_helper);

	fprintf(stderr,"done.\n");

	exit(0);
}//end signal_handler

//================================================================
void io_dump_config()
{
	if(io_())
	{
		//similar order to startup output
		//dont send easy to calculate fields (calc in gui)

		lo_message msgio=lo_message_new();

		//basic setup of a->b (localhost:port -> targethost:port)
		lo_message_add_int32(msgio,atoi(localPort));
//could info of sender, once receving
//		lo_message_add_string(msgio,sendToHost);
//		lo_message_add_int32(msgio,atoi(sendToPort));

		//local jack info
		lo_message_add_string(msgio,client_name);
		lo_message_add_string(msgio,server_name);

		lo_message_add_int32(msgio,sample_rate);
		lo_message_add_int32(msgio,period_size);

		lo_message_add_int32(msgio,output_port_count);
		lo_message_add_int32(msgio,channel_offset);

		//transmission info
		//this is not a property of local JACK
		lo_message_add_int32(msgio,bytes_per_sample);

		//multi-channel period size
		//lo_message_add_int32(msgio,output_port_count*period_size*bytes_per_sample);

		lo_message_add_int32(msgio,test_mode);
		lo_message_add_int32(msgio,receive_max);

//multi-channel period size
//		lo_message_add_float(msgio,output_port_count*period_size*bytes_per_sample);
		lo_message_add_int32(msgio,zero_on_underflow);
		lo_message_add_int32(msgio,rebuffer_on_restart);
		lo_message_add_int32(msgio,rebuffer_on_underflow);
		lo_message_add_int32(msgio,allow_remote_buffer_control);
		lo_message_add_int32(msgio,close_on_incomp);

//		fprintf(stderr,"receiving on TCP port: %s\n",localPort);
//		fprintf(stderr,"receiving on UDP port: %s\n",localPort);

//		lo_message_add_int32(msgio,max_buffer_size);
//should be global
//		lo_message_add_int32(msgio,rb_size);

		lo_send_message(loio, "/config_dump", msgio);
		lo_message_free(msgio);
	}//end if io_
}//end io_dump_config

