/*
JACK client listening to MIDI note on/off messages, toggling state and forwarding

--note on---> (jack_midi_on_off) --note off-->
--note off--> (jack_midi_on_off) --note on--->

gcc -o jack_midi_on_off midi_on_off.c  `pkg-config --libs liblo` `pkg-config --libs jack`

//tb/150908
*/


#include <stdio.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <sys/signal.h>
#include <sys/types.h>

int main(int argc, char *argv[]);
static void shutdown_callback(void *arg);
static void jack_error(const char* err);
static int process(jack_nframes_t nframes, void *arg);
static void signal_handler(int sig);

static jack_client_t *client;

static const char **ports;

static jack_port_t* port_in_midi;
static jack_port_t* port_out_midi;

void* buffer_in_midi;
void* buffer_out_midi;

static int process_enabled=0;
static int connection_to_jack_down=1;

uint64_t midi_events_received=0;
uint64_t midi_events_sent=0;

uint64_t midi_on_events_received=0;
uint64_t midi_off_events_received=0;

uint64_t midi_on_events_sent=0;
uint64_t midi_off_events_sent=0;

#define MODE_PASSTHRU 0     //forward input to output (any midi event)
#define MODE_TOGGLE 1       //toggle input (on->off, off->on) and forward

static int mode=MODE_TOGGLE;

//===================================================================
int main(int argc, char *argv[])
{
	// Make output unbuffered
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if(argc >= 2 &&
		(strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	{
		printf("connect JACK client\n\n");
		printf("syntax: jack_midi_on_off\n\n");

		printf("jack_midi_on_off source at https://github.com/7890/jack_tools\n\n");
		return(0);
	}

	//jack_options_t options=JackNullOption;
	jack_options_t options=JackNoStartServer;
	jack_status_t status;

	jack_set_error_function(jack_error);

	//outer loop, wait and reconnect to jack
	while(1==1)
	{
	connection_to_jack_down=1;
	fprintf(stderr,"\r\n");
	fprintf(stderr,"waiting for connection to JACK...\n\r");
	while(connection_to_jack_down)
	{
		if((client=jack_client_open("midi_on_off", options, &status, NULL))==0)
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

	port_in_midi = jack_port_register (client, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	port_out_midi = jack_port_register (client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

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
	fprintf(stderr, "\nsignal received, exiting ...\n");
	process_enabled=0;

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

//=============================================================================
static int process(jack_nframes_t nframes, void *arg)
{
	if(!process_enabled)
	{
		return 0;
	}

	//prepare receive buffer
	buffer_in_midi = jack_port_get_buffer (port_in_midi, nframes);
	buffer_out_midi = jack_port_get_buffer (port_out_midi, nframes);
	jack_midi_clear_buffer(buffer_out_midi);

	//process incoming messages from JACK
	int msgCount = jack_midi_get_event_count (buffer_in_midi);

	int i;
	//iterate over encapsulated osc messages
	for (i = 0; i < msgCount; ++i) 
	{
		jack_midi_event_t event;
		int r;

		r = jack_midi_event_get (&event, buffer_in_midi, i);
		if (r == 0)
		{
			//check if midi data, skip if other
			if(*event.buffer=='/')
			{
//				fprintf(stderr,"OSC> #%d / %d len %lu\n\r",i,msgCount,event.size);
				//do stuff here
			}
			else
			{
//				fprintf(stderr,"MIDI> #%d / %d len %lu\n\r",i,msgCount,event.size);//,event.buffer);

				int pos=0; //!! all events at pos 0 in cycle

				midi_events_received++;

				uint8_t type = event.buffer[0] & 0xf0;
				uint8_t channel = event.buffer[0] & 0xf;

				//if note on, send note off and vice versa
				if(type == 0x80) //off
				{
					midi_off_events_received++;
					//fprintf(stderr,"X");

					jack_midi_data_t *buffer;
					buffer = jack_midi_event_reserve(buffer_out_midi, pos, 3);
					buffer[2] = event.buffer[2],
					buffer[1] = event.buffer[1];

					if(mode==MODE_PASSTHRU)
					{
						buffer[0] = event.buffer[0];
						midi_off_events_sent++;
					}
					else if(mode==MODE_TOGGLE)
					{
						buffer[0] = 0x90 | (event.buffer[0] & 0xf); //on + channel
						midi_on_events_sent++;
					}
					midi_events_sent++;
				}
				else if(type == 0x90) //on
				{
					midi_on_events_received++;
					//fprintf(stderr,"_");
					jack_midi_data_t *buffer;
					buffer = jack_midi_event_reserve(buffer_out_midi, pos, 3);
					buffer[2] = event.buffer[2],
					buffer[1] = event.buffer[1];

					if(mode==MODE_PASSTHRU)
					{
						buffer[0] = event.buffer[0];
						midi_on_events_sent++;
					}
					else if(mode==MODE_TOGGLE)
					{
						buffer[0] = 0x80 | (event.buffer[0] & 0xf); //off + channel
						midi_off_events_sent++;
					}

					midi_events_sent++;
				}
				else if(mode==MODE_PASSTHRU)
				{
					jack_midi_data_t *buffer;
					buffer = jack_midi_event_reserve(buffer_out_midi, pos, 3);
					buffer[2] = event.buffer[2],
					buffer[1] = event.buffer[1];
					buffer[0] = event.buffer[0];
					midi_events_sent++;
				}
			}
		}
	}//end while has MIDI messages

	fprintf(stderr,"\r                                                                 \rin %" PRId64 " on %" PRId64 " off %" PRId64 " out %" PRId64 " on %" PRId64 " off %" PRId64 ""
		,midi_events_received
		,midi_on_events_received,midi_on_events_sent
		,midi_events_sent
		,midi_off_events_received,midi_off_events_sent);

	return 0;
}//end process()

//EOF
