#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jack/jack.h>
#include <unistd.h>
#include <signal.h>

//gcc -o register_jack_ports register_jack_ports.c `pkg-config --cflags --libs jack`

//tb/14

//test to register & connect many ports, run until ctrl+c, then cleanly shutdown
//first (mandatory) argument is number of ports (int)
//i.e. run in dummy jack with many ports

int input_port_count=100; //param

//Array of pointers to input or output ports
jack_port_t **ioPortArray;
jack_client_t *client;

jack_options_t jack_opts = JackNoStartServer;

int process_enabled=0;

//================================================================
void signal_handler(int sig)
{
	fprintf(stderr, "\nterminate signal %d received. cleaning up...",sig);
	process_enabled=0;

	jack_deactivate(client);

	int index=0;
	while(ioPortArray[index]!=NULL && index<input_port_count)
	{
		jack_port_unregister(client,ioPortArray[index]);
		index++;
	}

	jack_client_close(client);
	fprintf(stderr," done\n");
	exit(0);
}

//================================================================
int process(jack_nframes_t nframes, void *arg) 
{
	if(process_enabled==1)
	{
		fprintf(stderr,".");

		int i;
		for(i=0; i<input_port_count; i++)
		{
			if(process_enabled!=1)
			{
				return 0;
			}
			jack_default_audio_sample_t *o1;

			//get "the" buffer
			o1=(jack_default_audio_sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);
		}
	}
	return 0;
}

//================================================================
int main (int argc, char *argv[])
{
	fprintf(stderr,"first param is # of channels.\n");

	input_port_count=atoi(argv[1]);

	const char **ports;
	jack_status_t status;

	const char *client_name="testme";

	//create an array of input ports
	ioPortArray = (jack_port_t**) malloc(input_port_count * sizeof(jack_port_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, jack_opts, &status, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		exit(1);
	}
	jack_set_process_callback (client, process, NULL);

	// Register each output port
	int port=0;
	for (port=0 ; port<input_port_count ; port ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "input_%d", (port+1)) < 0) 
		{
			fprintf(stderr, "Could not create portname for port %d\n", port);
			exit(1);
		}

		// Register the input port
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if (ioPortArray[port] == NULL) 
		{
			fprintf(stderr, "Could not create input port %d\n", (port+1));
			exit(1);
		}
	}

	//now activate client in JACK, starting with process() cycles
	if (jack_activate (client)) 
	{
		fprintf (stderr, "cannot activate client\n\n");
		exit(1);
	}

	//prevent to get physical midi ports
	const char* pat="audio";

	ports = jack_get_ports (client, NULL, pat,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) 
	{
		fprintf(stderr, "no physical capture ports found\n");
		exit(1);
	}
	
	int autoconnect=1;
	if(autoconnect==1)
	{
		int j=0;
		int i=0;
		for(i=0;i<input_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& ioPortArray[j]!=NULL 
				&& jack_port_name(ioPortArray[j])!=NULL)
			{
				if(!jack_connect (client, ports[i],jack_port_name(ioPortArray[j])))
				{
					//used variabled can't be NULL here
					fprintf (stderr, "autoconnect: %s -> %s\n",
						ports[i],jack_port_name(ioPortArray[j])
					);
					j++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						ports[i],jack_port_name(ioPortArray[j])
					);
				}
			}
		}
	}

	free (ports);

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	process_enabled=1;

	//run possibly forever until not interrupted by any means
	while (1) 
	{
		sleep(1);
	}
	exit(0);
}//end main
