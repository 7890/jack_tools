#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <lo/lo.h>

//tb/140112/140516
//proof of concept to send arbitrary data over jack midi ports
//use osc as data structure
//tunnel osc through jack midi

//gcc -o jack_midi_tunnel_receive_osc midi_tunnel_receive_osc.c `pkg-config --libs jack liblo`

jack_client_t* client;
jack_port_t* port_in;
char const default_name[] = "midi_tunnel_receive_osc";
char const * client_name;

void* buffer_in;
char description[256];
int msgCount=0;
char* path;
char* types;

int msgsPerCycle=10;

int printEveryNthCycle=100;
int printCounter=-1;

int doPrint=0;

int strcmp();

static void signal_handler(int sig)
{
        jack_client_close(client);
        fprintf(stdout, "signal received, exiting ...\n");
        exit(0);
}

static int process (jack_nframes_t frames, void* arg)
{
	buffer_in = jack_port_get_buffer (port_in, frames);
	assert (buffer_in);

	msgCount = jack_midi_get_event_count (buffer_in);


	if(printCounter>=printEveryNthCycle || printCounter==-1)
	{
		doPrint=1;
		printCounter=0;
	}
	else
	{
		doPrint=0;
	}

	if(msgCount>0 && doPrint==1)
	{
		fprintf(stdout,"\nreceived midi osc events, count: %d\n",msgCount);
	}

	int i;
	//iterate over encapsulated osc messages
	for (i = 0; i < msgCount; ++i) 
	{
		jack_midi_event_t event;
		int r;

		r = jack_midi_event_get (&event, buffer_in, i);
		if (r == 0)
		{

			path=lo_get_path(event.buffer,event.size);

			int result;
			//some magic happens here
			lo_message msg = lo_message_deserialise(event.buffer, event.size, &result);
//			fprintf(stdout,"osc message (%i) size: %lu argc: %d\n",i+1,event.size,lo_message_get_argc(msg));

			types=lo_message_get_types(msg);

			lo_arg **argv = lo_message_get_argv(msg);
/*
			fprintf(stdout,"test: parameter 1 (int) is: %i \n",argv[0]->i);
			fprintf(stdout,"test: parameter 2 (int) is: %i \n",argv[1]->i);
			fprintf(stdout,"test: parameter 3 (float) is: %f \n",argv[2]->f);
			fprintf(stdout,"test: parameter 4 (string) is: %s \n",&argv[3]->s);

*/
			if(doPrint==1)
			{
				fprintf(stdout,"[%i %i] ",argv[0]->i,argv[1]->i);
			}

			lo_message_free(msg);
		}
	}

	printCounter++;

	return 0;
}

int main (int argc, char* argv[])
{
	client_name = default_name;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "Could not create JACK client.\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_in = jack_port_register (client, "midi_osc_input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	if (port_in == NULL) 
	{
		fprintf (stderr, "Could not register port.\n");
		return 1;
	}
	else
	{
		fprintf (stdout, "Registered JACK port.\n");
		fprintf (stdout, "Don't forget to connect input port with midi_tunnel_send_osc output port.\n");

	}

	if (jack_activate(client))
	{
		fprintf (stderr, "cannot activate client");
		return 1;
	}

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	/* run until interrupted */
	while (1) 
	{
		sleep(1);
	};

	jack_client_close(client);
	return 0;
}

/*
http://www.antoarts.com/void-pointers-in-c/?utm_source=rss&utm_medium=rss&utm_campaign=void-pointers-in-c

Void pointers are pointers pointing to some data of no specific type.

A void pointer is defined like a pointer of any other type, except that void* is used for the type:
void *pt;

You can’t directly dereference a void pointer; you must cast it to a pointer with a specific type first, for instance, to a pointer of type int*:
*(int*)pt;

Thus to assign a value to a void pointer, you will have to do something like:
*(int*)pt=42;
*(float*)pt=3.14; 

You can assign a value of any type to the pointer

The use of void pointers is mainly allowing for generic types. You can create data structures that can hold generic values, or you can have functions that take arguments of no specific type.
*/
