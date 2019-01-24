#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>
//#include <stdlib.h>

//gcc -o miditest_send miditest_send.c `pkg-config --libs jack`
//tb/14re19

jack_port_t *port_out;
void *buffer_out;
int quit_program=0;

static void signal_handler(int sig)
{
	fprintf(stderr, "Shutting down\n");
	quit_program=1;
}

static int process(jack_nframes_t frames, void *arg)
{
	if(quit_program==1){return 0;}
	buffer_out=jack_port_get_buffer(port_out, frames);
	jack_midi_clear_buffer(buffer_out);

//	fprintf(stderr, "Max MIDI event size: %d\n", jack_midi_max_event_size(buffer_out));

	int msg_size=512;
	char *buffer;
	buffer=calloc(msg_size, 1);

	//create sysex message
	buffer[0]=0xf0;
	int i;
	for(i=1;i<msg_size/2;i++) //filling 1/2 of sysex event
	{
		buffer[i]=0x42; //data
	}
	buffer[msg_size-1]=0xf7;

	//write two midi sysex data messages to output buffer
	int pos=0;
	jack_midi_event_write(buffer_out, pos, (void*)buffer, msg_size);
//	pos+=msg_size;
	jack_midi_event_write(buffer_out, pos, (void*)buffer, msg_size);

	free(buffer);

	return 0;
}

int main(int argc, char *argv[])
{
	jack_client_t *client;
	char const *client_name="miditest_send";

	client=jack_client_open(client_name, JackNullOption, NULL);
	if(client==NULL)
	{
		fprintf(stderr, "Error: could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback(client, process, 0);

	port_out=jack_port_register(client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if(port_out==NULL)
	{
		fprintf(stderr, "Error: could not register port\n");
		return 1;
	}
	else
	{
		fprintf(stderr, "Registered JACK MIDI output port 'out'\n");

	}

	if(jack_activate(client))
	{
		fprintf(stderr, "Error: cannot activate client");
		return 1;
	}

	signal(SIGINT, signal_handler);

	fprintf(stderr, "Ready\n");

	while(quit_program==0)
	{
		sleep(1);
	}

//	jack_port_unregister(client, port_out);
	jack_deactivate(client);
	jack_client_close(client);
	return 0;
}
//EOF
