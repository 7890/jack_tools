/*

gcc -o jack_midi_heartbeat midi_heartbeat.c `pkg-config --libs liblo` `pkg-config --libs jack`

//tb/150907 tom@trellis.ch
*/

#include <stdio.h>
#include <string.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <sys/signal.h>

int main(int argc, char *argv[]);
static void signal_handler(int sig);
static void shutdown_callback(void *arg);
static void jack_error(const char* err);
static int process(jack_nframes_t nframes, void *arg);

typedef jack_default_audio_sample_t sample_t;

static jack_client_t *client;

static const char **ports;
static jack_port_t **ioPortArray;

static jack_port_t* port_out_midi;

void* buffer_out_midi;

static int output_port_count=1;

static int process_enabled=0;
static int connection_to_jack_down=1;

//param
uint64_t frames_between_on_off=24000;
uint64_t frame_counter;
int is_on=0;

int velocity_counter=0;

//===================================================================
int main(int argc, char *argv[])
{
	// Make output unbuffered
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if(argc >= 2 &&
		(strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	{
		printf("connect JACK client that sends MIDI on/off events\n\n");
		printf("syntax: jack_midi_heartbeat <interval in samples>\n\n");
		printf("default value: 24000\n\n");
		printf("jack_midi_heartbeat source at https://github.com/7890/jack_tools\n\n");
		return(0);
	}

	//interval in samples
	if(argc >= 2)
	{
		frames_between_on_off=strtoull(argv[1], NULL, 10);
		fprintf(stderr,"setting interval to %"PRId64" frames.\n"
			,frames_between_on_off);
	}

	//jack_options_t options=JackNullOption;
	jack_options_t options=JackNoStartServer;
	jack_status_t status;

	ioPortArray = (jack_port_t**) calloc(
		output_port_count * sizeof(jack_port_t*), sizeof(jack_port_t*));

	jack_set_error_function(jack_error);

	//outer loop, wait and reconnect to jack
	while(1==1)
	{
	connection_to_jack_down=1;
	fprintf(stderr,"\r\n");
	fprintf(stderr,"waiting for connection to JACK...\n\r");
	while(connection_to_jack_down)
	{
		if((client=jack_client_open("midi_heartbeat", options, &status, NULL))==0)
		{
//			fprintf(stderr, "jack_client_open() failed, ""status=0x%2.0x\n", status);
			if(status & JackServerFailed)
			{
//				fprintf(stderr, "Unable to connect to JACK server\n");
			}
			usleep(1000000);
		}
		else
		{
			connection_to_jack_down=0;
			fprintf(stderr,"connected to JACK.\n\r");
		}
	}

	jack_on_shutdown(client, shutdown_callback, NULL);

	jack_set_process_callback (client, process, NULL);

	port_out_midi = jack_port_register (client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	//register each audio output port
	int port_=0;
	for (port_=0 ; port_<output_port_count ; port_ ++)
	{
		//create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port_+1)) < 0) 
		{
			fprintf(stderr, "/!\\ a could not create portname for port %d\n", port_);
			exit(1);
		}

		//register the output port
		ioPortArray[port_] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port_] == NULL) 
		{
			fprintf(stderr, "/!\\ b could not create output port %d\n", (port_+1));
			exit(1);
		}
	}

	if(jack_activate(client))
	{
		fprintf(stderr, "cannot activate client");
		exit(1);
	}

	process_enabled=1;

	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGTERM, signal_handler);

	while(1==1)
	{
		if(connection_to_jack_down)
		{
			goto _continue;
		}
		usleep(10000);
	}

_continue:

	usleep(10000);
}//end while true outer loop

	exit(0);
}//end main

//===================================================================
static void signal_handler(int sig)
{
	fprintf(stderr, "signal received, exiting ...\n");

	if(!connection_to_jack_down)
	{
		jack_client_close(client);
	}
	exit(0);
}

//================================================================
static void shutdown_callback(void *arg)
{
	connection_to_jack_down=1;
	fprintf(stderr, "JACK server down!\n");
}

//===================================================================
static void jack_error(const char* err)
{
	//suppress for now
}

//===================================================================
static void send_midi(int number,int ison,int velocity,int pos)
{
	jack_midi_data_t *buffer;
	buffer = jack_midi_event_reserve(buffer_out_midi, pos, 3);
		buffer[2] = velocity,
		buffer[1] = number;
	if(ison)
	{
		buffer[0] = 0x90;       /* note on */
	}
	else
	{
		buffer[0] = 0x80;       /* note off */
	}
}

//=============================================================================
static int process(jack_nframes_t nframes, void *arg)
{
	if(!process_enabled)
	{
		return 0;
	}

	//prepare receive buffer
	buffer_out_midi = jack_port_get_buffer (port_out_midi, nframes);
	jack_midi_clear_buffer(buffer_out_midi);

	sample_t *o1;
	//get output buffer from JACK for that channel
	o1=(sample_t*)jack_port_get_buffer(ioPortArray[0],nframes);

	//set all samples zero
	memset(o1, 0, nframes*4);

	int k=0;
	for(k=0;k<nframes;k++)
	{
		if(frame_counter % frames_between_on_off == 0)
		{
			fprintf(stderr,".");
			send_midi(42,is_on,velocity_counter,k);
			if(is_on)
			{
				o1[k]=0.8;
				is_on=0;
			}
			else
			{
				o1[k]=0;
				is_on=1;
			}
			velocity_counter++;
			if(velocity_counter>127)
			{
				velocity_counter=0;
			}
		}
		frame_counter++;
	}

	//fill audio buffers
/*
	int i=0;
	for(i=0; i<output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from JACK for that channel
		o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);

		//set all samples zero
//		memset(o1, 0, nframes*4);

		int k=0;
		for(k=0; k<nframes;k++)
		{
//			o1[k]=;
		}
	}
*/
	return 0;
}
