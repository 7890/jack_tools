#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <lo/lo.h>

//tb/140112/140516/140519
//receive jack internal midi osc messages and send out to network
//gcc -o jack_osc_bridge_out osc_bridge_out.c `pkg-config --libs jack liblo`

jack_client_t *client;
jack_port_t *port_in;
char const default_name[] = "osc_bridge_out";
char const * client_name;

void* buffer_in;
int msgCount=0;
char* path;
char* types;

lo_server_thread lo_st;
lo_address osc_target;

char * default_port="3345";
char * default_target_host="localhost";
char * default_target_port="3346";

int strcmp();

void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d: %s\n", num, msg);
	exit(1);
}


static void signal_handler(int sig)
{
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}

static int process(jack_nframes_t frames, void *arg)
{
	buffer_in = jack_port_get_buffer(port_in, frames);
	assert (buffer_in);

//	jack_midi_clear_buffer(buffer_out);

	msgCount = jack_midi_get_event_count (buffer_in);

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

			//types=lo_message_get_types(msg);
			//lo_arg **argv = lo_message_get_argv(msg);

			//fprintf(stdout,"osc message (%i) size: %lu argc: %d path: %s\n",i+1,event.size,lo_message_get_argc(msg),path);

			//lo_send(osc_target, "/foo/bar", "ff", 0.12345678f, 23.0f);
			lo_send_message(osc_target,path,msg);

			lo_message_free(msg);
		}
	}

	return 0;
}

int main(int argc, char* argv[])
{

	char cn[64];
	strcpy(cn,default_name);
	strcat(cn,"_");

	if(argc>1)
	{
		strcat(cn,argv[1]);
	}
	else
	{
		strcat(cn,default_port);
	}

	client_name=cn;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_in = jack_port_register (client, "midi_osc_input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	if (port_in == NULL) 
	{
		fprintf (stderr, "could not register port\n");
		return 1;
	}
	else
	{
		printf ("registered JACK port\n");
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

	if(argc>1)
	{
		lo_st = lo_server_thread_new(argv[1], error);
		printf("osc port: %s\n",argv[1]);
	}
	else
	{
		lo_st = lo_server_thread_new(default_port, error);
		printf("osc port: %s\n",default_port);
	}

	if(argc>3)
	{
		osc_target = lo_address_new(argv[2],argv[3]);
		printf("sending to: %s:%s\n",argv[2],argv[3]);
	}
	else
	{
		osc_target = lo_address_new(default_target_host,default_target_port);
		printf("sending to: %s:%s\n",default_target_host,default_target_port);
	}

	lo_server_thread_start(lo_st);

	/* run until interrupted */
	while (1) 
	{
		sleep(1);
	};

	jack_client_close(client);
	return 0;
}
