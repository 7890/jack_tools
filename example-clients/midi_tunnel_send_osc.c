#include <jack/jack.h>
#include <jack/midiport.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <lo/lo.h>

//tb/140112/140516
//proof of concept to send arbitrary data over jack midi ports
//use osc as data structure
//tunnel osc through jack midi

//gcc -o jack_midi_tunnel_send_osc midi_tunnel_send_osc.c  `pkg-config --libs jack liblo`

jack_client_t *client;
jack_port_t *output_port;

char const default_name[] = "midi_tunnel_send_osc";
jack_nframes_t nframes;
char const * client_name;

int strcmp();

static void signal_handler(int sig)
{
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

static int process(jack_nframes_t nframes, void *arg)
{
	void* port_buf = jack_port_get_buffer(output_port, nframes);
	jack_midi_clear_buffer(port_buf);

	//size_t jack_midi_max_event_size (void * port_buffer) 
	fprintf(stderr,"===\njack_midi_max_event_size: %lu\n",jack_midi_max_event_size(port_buf));

	//create osc message
	lo_message msg=lo_message_new();
	lo_message_add_int32(msg,1234);
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
	fprintf(stderr,"osc message (1) size: %lu\n",size);

/*
	int jack_midi_event_write ( 
		void * 		port_buffer,
		jack_nframes_t 	time,
		const jack_midi_data_t * data,
		size_t 		data_size 
	)
*/
	//write the serialized osc message to the midi buffer
	jack_midi_event_write(port_buf,0,pointer,size);
	//important to free resources
	lo_message_free(msg);

	//create and add a second message
	lo_message msg2=lo_message_new();
	lo_message_add_int32(msg2,9876);
	lo_message_add_float(msg2,9.876);
	lo_message_add_string(msg2,"the second msg");

	pointer=lo_message_serialise(msg2,"/another/message",NULL,&size);
	fprintf(stderr,"osc message (2) size: %lu\n",size);

	jack_midi_event_write(port_buf,0,pointer,size);
	lo_message_free(msg2);

	return 0;
}

int main(int argc, char* argv[])
{
	client_name = default_name;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "Could not create JACK client.\n");
		exit(1);
	}

	jack_set_process_callback (client, process, 0);

	output_port = jack_port_register (client, "midi_osc_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	nframes = jack_get_buffer_size(client);

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
	//exit (0);
	return 0;
}
