//jack_audio_common.c

#include <stdio.h>
#include <stdlib.h>
#include <jack/jack.h>
#include <lo/lo.h>

#include "jack_audio_common.h"

float version = 0.8f;
float format_version = 1.0f;

lo_server_thread lo_st;

jack_client_t *client;

//Array of pointers to input or output ports
jack_port_t **ioPortArray;

//Array of pointers to input or output buffers
jack_default_audio_sample_t **ioBufferArray;

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

void print_header (char *prgname)
{
	fprintf (stderr, "\n%s v%.2f (format v%.2f)\n", prgname,version,format_version);
	fprintf (stderr, "(C) 2013 - 2014 Thomas Brand  <tom@trellis.ch>\n");
}

void print_version ()
{
	fprintf (stderr, "%.2f\n",version);
	exit (0);
}

void periods_to_HMS(char *buf, uint64_t periods)
{
	//calculate elapsed time
	uint64_t seconds_elapsed_total=periods * period_size / sample_rate;
	uint64_t hours_elapsed_total=seconds_elapsed_total / 3600;
	uint64_t minutes_elapsed_total=seconds_elapsed_total / 60;

	uint64_t minutes_elapsed=minutes_elapsed_total % 60;
	uint64_t seconds_elapsed=seconds_elapsed_total % 60;

	sprintf(buf,"%02zu:%02zu:%02zu",
		hours_elapsed_total,minutes_elapsed,seconds_elapsed
	);
}

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

void read_jack_properties()
{
	sample_rate=jack_get_sample_rate(client);
	//! new: assume JACK is always 32 bit float
	//bytes_per_sample only refers to data transmission 
	//param --16 will force to use 2 bytes
	//bytes_per_sample = sizeof(jack_default_audio_sample_t);
	period_size=jack_get_buffer_size(client);
}

void print_common_jack_properties()
{
	fprintf(stderr,"sample rate: %d\n",sample_rate);

	if(bytes_per_sample==4)
	{
		fprintf(stderr,"bytes per sample: %d (32 bit float)\n",bytes_per_sample);
	}
	else
	{
		fprintf(stderr,"bytes per sample: %d (16 bit PCM)\n",bytes_per_sample);
	}

	char buf[64];
	format_seconds(buf,(float)period_size/(float)sample_rate);
	fprintf(stderr,"period size: %d samples (%s, %d bytes)\n",period_size,
		buf,period_size*bytes_per_sample
	);
}

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

