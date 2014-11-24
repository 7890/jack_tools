/* part of audio_rxtx
 *
 * Copyright (C) 2013 - 2014 Thomas Brand <tom@trellis.ch>
 *
 * This program is free software; feel free to redistribute it and/or 
 * modify it.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. bla.
*/

#include <stdio.h>
#include <stdlib.h>
//#include <jack/jack.h>//weak
#include <lo/lo.h>

#include "jack_audio_common.h"

float version = 0.86f;
float format_version = 1.1f;

lo_server_thread lo_st;

const char *server_name = NULL;
const char *client_name = NULL;

jack_client_t *client;

//Array of pointers to input or output ports
jack_port_t **ioPortArray;

//default values
//sample_rate must be THE SAME on sender and receiver

int sample_rate=44100;
int period_size=1024;

//2 bytes per sample: 16 bit (PCM wave)
//4 bytes per sample: 32 bit float
int bytes_per_sample=4;

//connect output to first n physical channels on startup
int autoconnect=0; //param

//int max_channel_count=64;
//int max_channel_count=256;
int max_channel_count=512;

jack_options_t jack_opts = JackNoStartServer;

int shutdown_in_progress=0;

uint64_t buffer_overflow_counter=0;

//don't stress the terminal with too many fprintfs in process()
int update_display_every_nth_cycle=99; //param
int relaxed_display_counter=0;

//test_mode (--limit) is handy for testing purposes
//if set to 1, program will terminate after receiving n messages
int test_mode=0;

//give lazy display a chance to output current value for last cycle
int last_test_cycle=0;

//store after how many frames all work is done in process()
int frames_since_cycle_start=0;
int frames_since_cycle_start_sum=0;
int frames_since_cycle_start_avg=0;

//reset fscs avg sum every 88. cycle
int fscs_avg_calc_interval=88;

//temporary counter (will be reset for avg calc)
int fscs_avg_counter=0;

int process_enabled=0;

int quiet=0;
int shutup=0;

int io_enabled=0;
int io_push_enabled=1;

char* io_host="localhost";
char* io_port="20220";

lo_address loio;

//local xrun counter (since start of this jack client)
uint64_t local_xrun_counter=0;

//=========================================================
void print_header (char *prgname)
{
	fprintf (stderr, "\n%s v%.2f (format v%.2f)\n", prgname,version,format_version);
	fprintf (stderr, "(C) 2013 - 2014 Thomas Brand  <tom@trellis.ch>\n");
}

//=========================================================
void print_version ()
{
	fprintf (stderr, "%.2f\n",version);
	exit (0);
}

//=========================================================
void periods_to_HMS(char *buf, uint64_t periods)
{
	//calculate elapsed time
	uint64_t seconds_elapsed_total=periods * period_size / sample_rate;
	uint64_t hours_elapsed_total=seconds_elapsed_total / 3600;
	uint64_t minutes_elapsed_total=seconds_elapsed_total / 60;

	uint64_t minutes_elapsed=minutes_elapsed_total % 60;
	uint64_t seconds_elapsed=seconds_elapsed_total % 60;

#ifdef _WIN
	sprintf(buf,"%02llu:%02llu:%02llu",
		hours_elapsed_total,minutes_elapsed,seconds_elapsed
	);
#else
	sprintf(buf,"%02" PRId64 ":%02" PRId64 ":%02" PRId64,
		hours_elapsed_total,minutes_elapsed,seconds_elapsed
	);
#endif

}

//=========================================================
void format_seconds(char *buf, float seconds)
{
	if(seconds>1)
	{
		sprintf(buf,"%.3f s",seconds);
	}
	else
	{
		sprintf(buf,"%.3f ms",seconds*1000);
	}
}

//=========================================================
void read_jack_properties()
{
	sample_rate=jack_get_sample_rate(client);
	//! new: assume JACK is always 32 bit float
	//bytes_per_sample only refers to data transmission 
	//param --16 will force to use 2 bytes
	//bytes_per_sample = sizeof(jack_default_audio_sample_t);
	period_size=jack_get_buffer_size(client);
}

//=========================================================
void print_common_jack_properties()
{
	fprintf(stderr,"sample rate: %d\n",sample_rate);

	char buf[64];
	format_seconds(buf,(float)period_size/(float)sample_rate);
	fprintf(stderr,"period size: %d samples (%s, %d bytes)\n",period_size,
		buf,period_size*bytes_per_sample
	);
}

//=========================================================
void print_bytes_per_sample()
{
	if(bytes_per_sample==4)
	{
		fprintf(stderr,"bytes per sample: %d (32 bit float)\n",bytes_per_sample);
	}
	else
	{
		fprintf(stderr,"bytes per sample: %d (16 bit PCM)\n",bytes_per_sample);
	}
}

//=========================================================
int check_lo_props(int debug)
{
	int ret=0;

	int major, minor, lt_maj, lt_min, lt_bug;
	char extra[256];
	char string[256];

	lo_version(string, 256,
		&major, &minor, extra, 256,
		&lt_maj, &lt_min, &lt_bug);

	if(debug==1)
	{
		printf("liblo version string `%s'\n", string);
		printf("liblo version: %d.%d%s\n", major, minor, extra);
	}

	if(major==0 && minor < 27)
	{
		printf(" /!\\ version < 0.27. audio-rxtx won't work, sorry.\n");
		ret=1;
	}

	if(debug==1)
	{
		printf("liblo libtool version: %d.%d.%d\n", lt_maj, lt_min, lt_bug);
		printf("liblo MAX_MSG_SIZE: %d\n", LO_MAX_MSG_SIZE);
	}

	if(debug==1)
	{
		printf("liblo LO_MAX_UDP_MSG_SIZE: %d\n", LO_MAX_UDP_MSG_SIZE);
	}
	if(LO_MAX_UDP_MSG_SIZE==LO_MAX_MSG_SIZE)
	{
		printf("/!\\ LO_MAX_UDP_MSG_SIZE is only %d\n",LO_MAX_UDP_MSG_SIZE);
	}

	if(debug==1)
	{
		printf("liblo LO_DEFAULT_MAX_MSG_SIZE: %d\n", LO_DEFAULT_MAX_MSG_SIZE);
	}
	if(LO_DEFAULT_MAX_MSG_SIZE==LO_MAX_MSG_SIZE)
	{
		printf("/!\\ LO_DEFAULT_MAX_MSG_SIZE is only %d\n",LO_DEFAULT_MAX_MSG_SIZE);
	}

	return ret;
}

//=========================================================
int io_()
{
	return (io_enabled==1 && io_push_enabled==1);
}

//=========================================================
void io_simple(char *path)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_simple_string(char *path, const char *string)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_string(msgio, string);
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_simple_string_double(char *path, const char *string1, const char *string2)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_string(msgio, string1);
		lo_message_add_string(msgio, string2);
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_simple_int(char *path, int i)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_int32(msgio, i);
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_simple_long(char *path, uint64_t l)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_int64(msgio, l);
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_simple_float(char *path, float f)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_float(msgio, f);
		lo_send_message(loio, path, msgio);
		lo_message_free(msgio);
	}
}

//=========================================================
void io_quit(char *token)
{
	if(io_())
	{
		lo_message msgio=lo_message_new();
		lo_message_add_string(msgio,token);
		lo_send_message(loio, "/quit", msgio);
		lo_message_free(msgio);
	}
}

//if JACK was shut down or the connection was otherwise lost
//=========================================================
void jack_shutdown_handler (void *arg)
{
	io_quit("jack_shutdown");

//main udp osc server
	lo_server_thread_free(lo_st);
//check for tcp
//

	exit (0);
}

//=========================================================
int xrun_handler()
{
	local_xrun_counter++;
	return 0;
}
