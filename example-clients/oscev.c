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

//tb/131020

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

jack_client_t *client;

lo_server_thread st;

//default port to send OSC messages from (my port)
const char* osc_my_server_port="6677";
//default host to send OSC messages
const char* osc_send_to_host="127.0.0.1";
//default port to send OSC messages
const char* osc_send_to_port="6678";

//osc server
lo_server_thread st;
lo_address loa;

//===================================================================
static void signal_handler(int sig)
{
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

//===================================================================
static void port_callback(jack_port_id_t port, int yn, void* arg)
{
	lo_message reply=lo_message_new();
	lo_message_add_int32(reply,port);

	if(yn)
	{
		lo_send_message(loa, "/oscev/port/registered", reply);
	}
	else
	{
		lo_send_message(loa, "/oscev/port/unregistered", reply);
	}
	lo_message_free(reply);

	printf("Port %d %s\n", port, (yn ? "registered" : "unregistered"));
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

	printf("Ports %d and %d %s\n", a, b, (yn ? "connected" : "disconnected"));
}

//===================================================================
static void client_callback(const char* client, int yn, void* arg)
{
	lo_message reply=lo_message_new();
	lo_message_add_string(reply,client);

	if(yn)
	{
		lo_send_message(loa, "/oscev/client/registered", reply);
	}
	else
	{
		lo_send_message(loa, "/oscev/client/unregistered", reply);
	}
	lo_message_free(reply);

	printf("Client %s %s\n", client, (yn ? "registered" : "unregistered"));
}

//===================================================================
static int graph_callback(void* arg)
{

	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/graph_reordered", reply);
	lo_message_free(reply);

	printf("Graph reordered\n");
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
		printf("  /oscev/client/registered s \"meter\"\n");
		printf("  /oscev/port/registered i 24\n");
		printf("  /oscev/port/connected ii 2 24\n");
		printf("  /oscev/port/disconnected ii 2 24\n");
		printf("  /oscev/port/unregistered i 24\n");
		printf("  /oscev/client/unregistered s \"meter\"\n\n");
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
	loa=lo_address_new(osc_send_to_host, osc_send_to_port);

	lo_server_thread_start(st);

	lo_message reply=lo_message_new();
	lo_send_message(loa, "/oscev/started", reply);
	lo_message_free(reply);

	jack_options_t options=JackNullOption;
	jack_status_t status;

	if((client=jack_client_open("oscev", options, &status, NULL))==0)
	{
		fprintf(stderr, "jack_client_open() failed, "
			 "status=0x%2.0x\n", status);
		if(status & JackServerFailed)
		{
			fprintf(stderr, "Unable to connect to JACK server\n");
		}
		return 1;
	}
	
	if(jack_set_port_registration_callback(client, port_callback, NULL))
	{
		fprintf(stderr, "cannot set port registration callback\n");
		return 1;
	}
	if(jack_set_port_connect_callback(client, connect_callback, NULL))
	{
		fprintf(stderr, "cannot set port connect callback\n");
		return 1;
	}
	if(jack_set_client_registration_callback(client, client_callback, NULL))
	{
		fprintf(stderr, "cannot set client registration callback\n");
		return 1;
	}
/*
	//don't register for now
	if(jack_set_graph_order_callback(client, graph_callback, NULL)) {
		fprintf(stderr, "cannot set graph order registration callback\n");
		return 1;
	}
*/
	if(jack_activate(client))
	{
		fprintf(stderr, "cannot activate client");
		return 1;
	}
    
#ifndef WIN32
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
#endif
	signal(SIGABRT, signal_handler);
	signal(SIGTERM, signal_handler);

#ifdef WIN32
	Sleep(INFINITE);
#else
	sleep(-1);
#endif
	exit(0);
}//end main
