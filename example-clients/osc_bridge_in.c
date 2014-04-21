#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <lo/lo.h>

//tb/140112/140421

//receive osc messages from any osc sender and feed it into the jack ecosystem as midi osc events
//gcc -o jack_osc_bridge_in osc_bridge_in.c `pkg-config --libs jack liblo`

jack_client_t *client;
jack_port_t *port_out;
char const default_name[] = "osc_bridge_in";
char const * client_name;

void* buffer_out;

jack_ringbuffer_t *rb;
lo_server_thread lo_st;

char * default_port="3344";

int strcmp();

void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d: %s\n", num, msg);
	exit(1);
}

int default_msg_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	//get size of message incl. path
	size_t size;
	void* msg_ptr=lo_message_serialise(data,path,NULL,&size);

	size_t can_write=jack_ringbuffer_write_space(rb);
	//printf("size %lu (+%lu bytes delim) can write %lu\n",size,sizeof(size_t),can_write);

	if(size+sizeof(size_t)<=can_write)
	{
		//write size of message
		int cnt=jack_ringbuffer_write(rb, &size, sizeof(size_t));
		//write message
		cnt+=jack_ringbuffer_write(rb, (void *) msg_ptr, size );
		//printf("%i\n",cnt);
		printf("+");
	}
	else
	{
		printf("ringbuffer full, can't write! message is lost\n");
		return 1;
	}

	return 0;
}


static void signal_handler(int sig)
{
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}

static int process(jack_nframes_t frames, void *arg)
{
	buffer_out = jack_port_get_buffer(port_out, frames);
	assert (buffer_out);

	jack_midi_clear_buffer(buffer_out);

	while(jack_ringbuffer_read_space(rb)>sizeof(size_t))
	{	
		size_t can_read = jack_ringbuffer_read_space(rb);
		//printf("can read %lu\n",can_read);

		size_t msg_size;
		jack_ringbuffer_read (rb, &msg_size, sizeof(size_t));
		//printf("msg_size %lu\n",msg_size);

		void* buffer = malloc(msg_size);

		//read message from ringbuffer to msg_buffer
		jack_ringbuffer_read (rb, (void *)buffer, msg_size);
/*
		char* path=lo_get_path(buffer,msg_size);
		printf("path %s\n",path);

		lo_message m=lo_message_deserialise(buffer,msg_size,NULL);
		lo_arg** args=lo_message_get_argv(m);
		int argc=lo_message_get_argc(m);
		printf("argc %d\n",argc);
		printf("arg 0 %d\n",args[0]->i);
		printf("arg 1 %s\n",&args[1]->s);
		lo_message_free(m);
*/

		if(msg_size <= jack_midi_max_event_size(buffer_out))
		{
			//write osc message to output buffer
			jack_midi_event_write(buffer_out,0,buffer,msg_size);
			printf("-");
		}
		else
		{
			fprintf(stderr,"available jack midi buffer size was too small! message lost\n");
		}
		free(buffer);
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

	port_out = jack_port_register (client, "midi_osc_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (port_out == NULL) 
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


	rb=jack_ringbuffer_create (100000);

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

	//match any path, any types
	lo_server_thread_add_method(lo_st, NULL, NULL, default_msg_handler, NULL);
	lo_server_thread_start(lo_st);

	/* run until interrupted */
	while (1) 
	{
		sleep(1);
	};

	jack_client_close(client);
	return 0;
}
