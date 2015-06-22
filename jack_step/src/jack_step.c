#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>

#include <iostream>
#include <string>

//#include <jack/jack.h>
//#include <jack/ringbuffer.h>
#include "weak_libjack.h"

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

#ifdef WIN32
	#define bzero(p, l) memset(p, 0, l)
#endif

typedef jack_default_audio_sample_t sample_t;

//tb/150619

//attempt to step jack cycles

//array of pointers to jack input or output ports
static jack_port_t **ioPortArray;
static jack_client_t *client;
static jack_options_t jack_opts = JackNoStartServer;

int process_enabled=0;

//don't create any ports for now
int output_port_count=0;

int shutdown_in_progress=0;
int shutdown_in_progress_signalled=0;

int is_freewheeling=0;
int cycle_count=0;

//================================================================
void signal_handler(int sig)
{

	process_enabled=0;

	//ctrl+c while running: go to freewheeling mode
	if(sig==SIGINT && !is_freewheeling)
	{
		jack_set_freewheel(client,1);
		return;
	}

	//else shutdown

	shutdown_in_progress=1;

	jack_set_freewheel(client,0);

	jack_deactivate(client);
	//fprintf(stderr,"jack client deactivated. ");

	int index=0;
	while(ioPortArray[index]!=NULL && index<output_port_count)
	{
		jack_port_unregister(client,ioPortArray[index]);
		index++;
	}
	//fprintf(stderr,"jack ports unregistered. ");

	jack_client_close(client);
	fprintf(stderr,"jack client closed. ");

	fprintf(stderr,"done\n");
	exit(0);
}

//================================================================
void jack_shutdown_handler (void *arg)
{
	fprintf(stderr, "jack server down!\n");
	exit(1);	
}

int return_to_normal_operation=0;

//================================================================
int process(jack_nframes_t nframes, void *arg) 
{
	if(!process_enabled
		|| shutdown_in_progress)
	{
		return 0;
	}

	//fill some samples
/*
	for(int i=0; i<output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from jack for that channel
		o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);
		//set all samples zero
		//memset(o1, 0, nframes*4);
		//output something
		for(int j=0;j<nframes;j++){o1[j]=(j%2)*0.3;}
	}
*/

	if(is_freewheeling)
	{
		fprintf(stderr,".%d ",cycle_count);

		char c;
		while (c=getchar()) 
		{
			if(c=='\n') //next cycle
			{
				break;
			}
			else if(c=='c') //resume
			{
				return_to_normal_operation=1;
				process_enabled=0;
			}
		}
	}
	else
	{
		fprintf(stderr,"=%d ",cycle_count);
	}
	cycle_count++;

	return 0;
}

//================================================================
void freewheel(int isfw, void *arg)
{
	is_freewheeling=isfw;
	fprintf(stderr,"JACK went to freewheel mode %d\n"
		,is_freewheeling);

	if(is_freewheeling)
	{
		fprintf(stderr,"press enter to step one JACK cycle\nctrl+c+enter to quit\n'c+enter' to resume\n");
	}

	process_enabled=1;
}

//================================================================
int main(int argc, char *argv[])
{
	const char **ports;
	jack_status_t status;

	const char *client_name="jack_step";

	//create an array of output ports
	ioPortArray = (jack_port_t**) malloc(output_port_count * sizeof(jack_port_t*));

	//open a client connection to the JACK server
	client = jack_client_open (client_name, jack_opts, &status, NULL);

	if (client == NULL) 
	{
		fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		exit(1);
	}

	jack_set_process_callback (client, process, NULL);
	jack_set_freewheel_callback(client, freewheel, NULL);

	//register hook to know when JACK shuts down or the connection 
	//was lost (i.e. client zombified)
	jack_on_shutdown(client, jack_shutdown_handler, 0);

	// Register each output port
	int port=0;
	for (port=0 ; port<output_port_count ; port ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port+1)) < 0) 
		{
			fprintf(stderr, "Could not create portname for port %d\n", port);
			exit(1);
		}

		// Register the output port
		ioPortArray[port] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port] == NULL) 
		{
			fprintf(stderr, "Could not create output port %d\n", (port+1));
			exit(1);
		}
	}

	//now activate client in JACK, starting with process() cycles
	if (jack_activate (client)) 
	{
		fprintf (stderr, "cannot activate client\n\n");
		exit(1);
	}

	const char* pat="audio";

	ports = jack_get_ports (client, NULL, pat,
				JackPortIsPhysical|JackPortIsTerminal|JackPortIsInput);
	if (ports == NULL) 
	{
		fprintf(stderr, "no physical capture ports found\n");
		exit(1);
	}
	
	int autoconnect=1;
	if(autoconnect)
	{
		int j=0;
		int i=0;
		for(i=0;i<output_port_count;i++)
		{
			if (ports[i]!=NULL 
				&& ioPortArray[j]!=NULL 
				&& jack_port_name(ioPortArray[j])!=NULL)
			{
				if(!jack_connect (client, jack_port_name(ioPortArray[j]) , ports[i]))
				{
					//used variabled can't be NULL here
					fprintf (stderr, "autoconnect: %s -> %s\n",
						jack_port_name(ioPortArray[j]),ports[i]
					);
					j++;
				}
				else
				{
					fprintf (stderr, "autoconnect: failed: %s -> %s\n",
						jack_port_name(ioPortArray[j]),ports[i]
					);
				}
			}
		}
	}

	free (ports);

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

//	jack_set_freewheel(client,1);

	process_enabled=1;

	//run possibly forever until not interrupted by any means
	while (1) 
	{
		if(return_to_normal_operation==1)
		{
			jack_set_freewheel(client,0);
			return_to_normal_operation=0;
		}

#ifdef WIN32
		Sleep(1000);
#else
		usleep(10000);
#endif
	}
	exit(0);
}//end main
