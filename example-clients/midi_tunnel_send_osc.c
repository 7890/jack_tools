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

//gcc -o jack_midi_tunnel_send_osc midi_tunnel_send_osc.c  `pkg-config --libs jack liblo`

jack_client_t *client;
jack_port_t *port_out;
char const default_name[] = "midi_tunnel_send_osc";
char const * client_name;

int counter=1;

void* buffer_out;

int msgsPerCycle=10;

int printEveryNthCycle=100;
int printCounter=-1;

int strcmp();

static void signal_handler(int sig)
{
	jack_client_close(client);
	fprintf(stdout, "signal received, exiting ...\n");
	exit(0);
}

static int process(jack_nframes_t frames, void *arg)
{
	buffer_out = jack_port_get_buffer(port_out, frames);
	assert (buffer_out);

	jack_midi_clear_buffer(buffer_out);

	//size_t jack_midi_max_event_size (void * buffer_outfer) 
	//fprintf(stdout,"===\njack_midi_max_event_size: %lu\n",jack_midi_max_event_size(buffer_out));

	int i;
	for(i=1;i<=msgsPerCycle;i++)
	{

		//create osc message
		lo_message msg=lo_message_new();
		lo_message_add_int32(msg,counter);
		lo_message_add_int32(msg,i);
		lo_message_add_float(msg,1.234);
		lo_message_add_string(msg,"here comes the string & some chars <àéè>");

		size_t size;
		void *pointer;

/*
		void* lo_message_serialise ( 
			lo_message 	m,
			const char * 	path,
			void * 		to,
			size_t * 	size 
		) 
*/
		//some magic happens here
		pointer=lo_message_serialise(msg,"/hello/jack/midi",NULL,&size);

		if(printCounter>=printEveryNthCycle || printCounter==-1)
		{
			fprintf(stdout,"osc message [%i %i/%i] size: %lu\n",counter,i,msgsPerCycle,size);
			printCounter=0;
		}
		//write the serialized osc message to the midi buffer
		jack_midi_event_write(buffer_out,0,pointer,size);
		//important to free resources
		lo_message_free(msg);
		free(pointer);
	}

	counter++;
	printCounter++;

	return 0;
}

int main(int argc, char* argv[])
{
	client_name = default_name;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "Could not create JACK client.\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_out = jack_port_register (client, "midi_osc_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

        if (port_out == NULL) 
        {
                fprintf (stderr, "Could not register port.\n");
                return 1;
        }
        else
        {
                fprintf (stdout, "Registered JACK port.\n");

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
