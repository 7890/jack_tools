#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <lo/lo.h>

//tb/140421

//test filter (receive, analyze, modify, (re-)send)
//gcc -o jack_osc_filter osc_filter.c `pkg-config --libs jack liblo`

static jack_port_t* port_in;
static jack_port_t* port_out;
jack_client_t* client;
char const default_name[] = "osc_filter";
char const * client_name;

int strcmp();

int counter=0;

void* buffer_in;

char* path;
char* types;

void* buffer_out;

int nextReceived=0;
int nextRequested=0;
int processing=0;
int basketAvailable=0;
int ancestorNextRequested=0;

static int process (jack_nframes_t frames, void* arg)
{


//prepare receive

	buffer_in = jack_port_get_buffer (port_in, frames);
	assert (buffer_in);


//prepare send

	buffer_out = jack_port_get_buffer(port_out, frames);
	assert (buffer_out);
	jack_midi_clear_buffer(buffer_out);

//receive
	int msgCount = jack_midi_get_event_count (buffer_in);
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

			//match path
			if(!strcmp(path,"/hello"))
			{
				//////////
			}

			int result;
			lo_message msg = lo_message_deserialise(event.buffer, event.size, &result);
			//printf("osc message (%i) size: %lu argc: %d\n",i+1,event.size,lo_message_get_argc(msg));

			types=lo_message_get_types(msg);
			//printf("types %s path %s\n",types,path);

			//match types
			if(!strcmp(types,"fis"))
			{
				//////////
			}

			lo_arg **argv = lo_message_get_argv(msg);
			//printf("test: parameter 1 (float) is: %f \n",argv[0]->f);
			//printf("test: parameter 2 (int) is: %i \n",argv[1]->i);
			//printf("test: parameter 3 (string) is: %s \n",&argv[2]->s);
			
			//match arg
			if(argv[1]->i==123)
			{
				//////////
			}			

			//add arg
			lo_message_add_int32(msg,887766);

			//(re-)send

			size_t msg_size;
			void *pointer;

			//rewrite path
			pointer=lo_message_serialise(msg,"/hello/jack/midi",NULL,&msg_size);

			if(msg_size <= jack_midi_max_event_size(buffer_out))
			{
				//write the serialized osc message to the midi buffer
				jack_midi_event_write(buffer_out,0,pointer,msg_size);
			}
			else
			{
				fprintf(stderr,"available jack midi buffer size was too small! message lost\n");
			}

			//free resources
			lo_message_free(msg);
			free(pointer);
		}
	}
	return 0;
}

static void signal_handler(int sig)
{
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}

int main (int argc, char* argv[])
{
	client_name = default_name;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		printf ("could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_in = jack_port_register (client, "midi_osc_input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	port_out = jack_port_register (client, "midi_osc_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (port_in == NULL || port_out == NULL) 
	{
		fprintf (stderr, "could not register port\n");
		return 1;
	}
	else
	{
		printf ("registered JACK ports\n");
	}

	int r = jack_activate (client);
	if (r != 0) 
	{
		fprintf (stderr, "could not activate client\n");
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
		//sleep(1);
		usleep(100000);
	};

	jack_client_close(client);
	return 0;
}
