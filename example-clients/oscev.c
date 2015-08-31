/*
    Copyright (C) 2007 Paul Davis
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


//origin file jackd2-1.9.8~dfsg.1/example-clients/evmon.c
//(jack_evmon binary in jackd packages)

//send jack events as OSC messages 
//gcc -o jack_oscev oscev.c `pkg-config --libs liblo` `pkg-config --libs jack`

//tb/131020/150831

#include <stdio.h>
#include <errno.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <signal.h>
#include <stdlib.h>

#include <jack/jack.h>

#include "lo/lo.h"

static jack_client_t *client;

static int connection_to_jack_down=1;
int64_t xrun_counter=0;
static jack_transport_state_t transport_state=-1;
static transport_state_prev=-2; //provocate notification at start

//default port to send OSC messages from (my port)
static const char* osc_my_server_port="6677";
//default host to send OSC messages
static const char* osc_send_to_host="127.0.0.1";
//default port to send OSC messages
static const char* osc_send_to_port="6678";

//osc server
static lo_server_thread st;
static lo_address loa;

//===================================================================
static void signal_handler(int sig)
{
	fprintf(stderr, "signal received, exiting ...\n");

	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/terminated", reply);
	lo_message_free(reply);

	if(!connection_to_jack_down)
	{
		jack_client_close(client);
	}
	exit(0);
}

//===================================================================
static void port_callback(jack_port_id_t a, int yn, void* arg)
{
	lo_message reply=lo_message_new();
	lo_message_add_int32(reply,a);

	if(yn)
	{
		jack_port_t *port=jack_port_by_id(client,a);
		const char *port_name=jack_port_name(jack_port_by_id(client,a));
		lo_message_add_string(reply,port_name);
		lo_message_add_string(reply,jack_port_type(port));

		int flags=jack_port_flags(port);
		if(flags & JackPortIsInput)
		{
			lo_message_add_int32(reply,1);
		}
		else
		{
			lo_message_add_int32(reply,0);
		}
		if(flags & JackPortIsOutput)
		{
			lo_message_add_int32(reply,1);
		}
		else
		{
			lo_message_add_int32(reply,0);
		}
		if(flags & JackPortCanMonitor)
		{
			lo_message_add_int32(reply,1);
		}
		else
		{
			lo_message_add_int32(reply,0);
		}
		if(flags & JackPortIsPhysical)
		{
			lo_message_add_int32(reply,1);
		}
		else
		{
			lo_message_add_int32(reply,0);
		}
		if(flags & JackPortIsTerminal)
		{
			lo_message_add_int32(reply,1);
		}
		else
		{
			lo_message_add_int32(reply,0);
		}

		lo_send_message(loa, "/oscev/port/registered", reply);
	}
	else
	{
		lo_send_message(loa, "/oscev/port/unregistered", reply);
	}
	lo_message_free(reply);

	fprintf(stderr,"port %d %s\n", a, (yn ? "registered" : "unregistered"));
}

//===================================================================
static void connect_callback(jack_port_id_t a, jack_port_id_t b, int yn, void* arg)
{
	lo_message reply=lo_message_new();
	lo_message_add_int32(reply,a);

	lo_message_add_int32(reply,b);

	if(yn)
	{
		lo_send_message(loa, "/oscev/port/connected", reply);
	}
	else
	{
		lo_send_message(loa, "/oscev/port/disconnected", reply);
	}
	lo_message_free(reply);

	fprintf(stderr,"ports %d and %d %s\n", a, b, (yn ? "connected" : "disconnected"));
}

//===================================================================
static void client_callback(const char* client_name, int yn, void* arg)
{
	lo_message reply=lo_message_new();
	char *client_id=jack_get_uuid_for_client_name(client, client_name);
	if(client_id!=NULL)
	{
		lo_message_add_string(reply,client_id);
	}

	lo_message_add_string(reply,client_name);

	if(yn)
	{
		lo_send_message(loa, "/oscev/client/registered", reply);
	}
	else
	{
		lo_send_message(loa, "/oscev/client/unregistered", reply);
	}
	lo_message_free(reply);

	fprintf(stderr,"client %s %s\n", client_name, (yn ? "registered" : "unregistered"));
}

//===================================================================
static void freewheel(int isfw, void *arg)
{
	lo_message reply=lo_message_new();

	if(isfw)
	{
		lo_message_add_int32(reply,1);
	}
	else
	{
		lo_message_add_int32(reply,0);
	}

	lo_send_message(loa, "/oscev/jack/freewheeling", reply);
	lo_message_free(reply);

	fprintf(stderr,"JACK freewhelling %d\n",isfw);
}

//================================================================
static int xrun_callback(void *arg)
{
	xrun_counter++;
	lo_message reply=lo_message_new();
	lo_message_add_int64(reply,xrun_counter);
	lo_send_message(loa, "/oscev/jack/xrun", reply);
	lo_message_free(reply);

	fprintf(stderr, "xrun!\n");
}

//================================================================
static void shutdown_callback(void *arg)
{
	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/jack/down", reply);
	lo_message_free(reply);

	connection_to_jack_down=1;

	fprintf(stderr, "JACK server down!\n");
}

//===================================================================
static int graph_callback(void* arg)
{
	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/graph_reordered", reply);
	lo_message_free(reply);

	fprintf(stderr,"graph reordered\n");
	return 0;
}

//===================================================================
static void jack_error(const char* err)
{
	//suppress for now
}

//=============================================================================
static int process(jack_nframes_t nframes, void *arg)
{
	if(transport_state!=-2)
	{
		transport_state_prev=transport_state;
	}
	transport_state=jack_transport_query(client, NULL);

	if(transport_state!=transport_state_prev)
	{
		lo_message reply=lo_message_new();
		/*
		JackTransportStarting  3
		JackTransportRolling   1
		JackTransportStopped   0
		*/
		lo_message_add_int32(reply,(int)transport_state);

		lo_send_message(loa, "/oscev/transport", reply);
		lo_message_free(reply);
	}
	return 0;
}

//===================================================================
int main(int argc, char *argv[])
{
	// Make output unbuffered
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if(argc >= 2 &&
		(strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	{
		printf("send jack events as OSC message\n\n");
		printf("syntax: jack_oscev <osc local port> <osc remote host> <osc remote port>\n\n");
		printf("all params are optional. order matters.\n");
		printf("default values: 6677 127.0.0.1 6678\n");
		printf("example: jack_oscev 9988 10.10.10.42\n");
		printf("test on .42: oscdump 6678\n\n");
		printf("messages sent by jack_oscev (example content):\n");
		printf("  /oscev/started\n");
		printf("  /oscev/transport i 0\n");
		printf("  /oscev/jack ii 48000 64\n");
		printf("  /oscev/ready\n");
		printf("  /oscev/error\n");
		printf("  /oscev/client/registered s \"player\"\n");
		printf("  /oscev/port/registered is 24 \"player:out\"\n");
		printf("  /oscev/port/registered issiiiii 24 \"player:out\" \"32 bit float mono audio\" 0 1 0 0 0\n");
		printf("  /oscev/port/connected ii 2 24\n");
		printf("  /oscev/port/disconnected ii 2 24\n");
		printf("  /oscev/port/unregistered i 24\n");
		printf("  /oscev/client/unregistered s \"player\"\n");
		printf("  /oscev/jack/down\n");
		printf("  /oscev/jack/xrun h 4\n");
		printf("  /oscev/jack/freewheeling i 1\n\n");
		//printf("");

		printf("jack_oscev source at https://github.com/7890/jack_tools\n\n");
		return(0);
	}

	//remote port
	if(argc >= 4)
	{
		osc_send_to_port=argv[3];
	}

	if(argc >= 3)
	{
		osc_send_to_host=argv[2];
	}

	//local port
	if(argc >= 2)
	{
		osc_my_server_port=argv[1];
	}
 
	//init osc
	st=lo_server_thread_new(osc_my_server_port, NULL);

	if(st==NULL)
	{
		fprintf(stderr,"could not start OSC server on port %s\n",osc_my_server_port);
		exit(1);
	}
	else
	{
		fprintf(stderr,"127.0.0.1:%s -> %s:%s\n",osc_my_server_port,osc_send_to_host, osc_send_to_port);
	}

	loa=lo_address_new(osc_send_to_host, osc_send_to_port);

	lo_server_thread_start(st);

	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/started", reply);
	lo_message_free(reply);

	//jack_options_t options=JackNullOption;
	jack_options_t options=JackNoStartServer;
	jack_status_t status;

	jack_set_error_function(jack_error);

	//outer loop, wait and reconnect to jack
	while(1==1)
	{
	connection_to_jack_down=1;
	fprintf(stderr,"waiting for connection to JACK");
	while(connection_to_jack_down)
	{
		if((client=jack_client_open("oscev", options, &status, NULL))==0)
		{
//			fprintf(stderr, "jack_client_open() failed, ""status=0x%2.0x\n", status);
			if(status & JackServerFailed)
			{
//				fprintf(stderr, "Unable to connect to JACK server\n");
			}
//			fprintf(stderr,".");
			//goto _error;
			usleep(1000000);
		}
		else
		{
			connection_to_jack_down=0;
			fprintf(stderr,"\r                                    \rconnected to JACK.\n");
		}
	}

	if(jack_set_port_registration_callback(client, port_callback, NULL))
	{
		fprintf(stderr, "cannot set port registration callback\n");
		goto _error;
	}

//typedef int(* 	JackBufferSizeCallback )(jack_nframes_t nframes, void *arg)
//typedef int(* 	JackSampleRateCallback )(jack_nframes_t nframes, void *arg)
//typedef void(* 	JackPortRenameCallback )(jack_port_id_t port, const char *old_name, const char *new_name, void *arg)

	if(jack_set_port_connect_callback(client, connect_callback, NULL))
	{
		fprintf(stderr, "cannot set port connect callback\n");
		goto _error;
	}
	if(jack_set_client_registration_callback(client, client_callback, NULL))
	{
		fprintf(stderr, "cannot set client registration callback\n");
		goto _error;
	}
/*
	//don't register for now
	if(jack_set_graph_order_callback(client, graph_callback, NULL)) {
		fprintf(stderr, "cannot set graph order registration callback\n");
		goto _error;
	}
*/
	if(jack_set_freewheel_callback(client, freewheel, NULL))
	{
		fprintf(stderr, "cannot set freewheel callback\n");
		goto _error;
	}

	jack_set_xrun_callback(client, xrun_callback, NULL);

	jack_on_shutdown(client, shutdown_callback, NULL);

	transport_state=-2;
	transport_state_prev=-1;
	jack_set_process_callback (client, process, NULL);

	if(jack_activate(client))
	{
		fprintf(stderr, "cannot activate client");
		goto _error;
	}

	reply=lo_message_new();
	lo_message_add_int32(reply,(int)jack_get_sample_rate(client));
	lo_message_add_int32(reply,(int)jack_get_buffer_size(client));
	lo_send_message(loa, "/oscev/jack", reply);
	lo_message_free(reply);

	reply=lo_message_new();
	lo_send_message(loa, "/oscev/ready", reply);
	lo_message_free(reply);

#ifndef WIN32
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
#endif
	signal(SIGABRT, signal_handler);
	signal(SIGTERM, signal_handler);

//#ifdef WIN32
//	Sleep(INFINITE);
//#else
//	sleep(-1);
//#endif
//	exit(0);

	while(1==1)
	{
		if(connection_to_jack_down)
		{
			goto _continue;
		}
		usleep(10000);
	}

_error:
	reply=lo_message_new();
	lo_send_message(loa, "/oscev/error", reply);
	lo_message_free(reply);

_continue:
	usleep(10000);
}//end while true outer loop

	exit(0);
}//end main
