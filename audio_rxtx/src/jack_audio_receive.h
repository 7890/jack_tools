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

#ifndef JACK_AUDIO_RECEIVE_H_INCLUDED
#define JACK_AUDIO_RECEIVE_H_INCLUDED

/*
structure:

	-variables

	-help

	-options struct

	-method declarations

		-main

		-process

		-osc + error handler

		-helpers

*/

int close_on_incomp=0; //param

int rebuffer_on_restart=0; //param
int rebuffer_on_underflow=0; //param

int channel_offset=0; //param

//if set to 0, /buffer ii messages are ignored
extern int allow_remote_buffer_control; //param

//if no data is available, fill with zero (silence)
//if set to 0, the current wavetable will be used
//if the network cable is plugged out, this can sound awful

extern int zero_on_underflow; //param

//================================================================
static void print_help (void)
{
	fprintf (stderr, "Usage: jack_audio_receive [Options] listening_port.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text and quit         --help\n");
	fprintf (stderr, "  Show program version and quit      --version\n");
	fprintf (stderr, "  Show liblo properties and quit     --loinfo\n");
	fprintf (stderr, "  Number of playback channels    (2) --out    <integer>\n");
	fprintf (stderr, "  Channel Offset                 (0) --offset <integer>\n");
	fprintf (stderr, "  Autoconnect ports                  --connect\n");
	fprintf (stderr, "  Send 16 bit samples (32 bit float) --16\n");
	fprintf (stderr, "  JACK client name         (receive) --name   <string>\n");
	fprintf (stderr, "  JACK server name         (default) --sname  <string>\n");
	fprintf (stderr, "  Initial buffer size (4 mc periods) --pre    <integer>\n");
	fprintf (stderr, "  Max buffer size (>= init)   (auto) --max    <integer>\n");
	fprintf (stderr, "  Rebuffer on sender restart         --rere\n");
	fprintf (stderr, "  Rebuffer on underflow              --reuf\n");
	fprintf (stderr, "  Re-use old data on underflow       --nozero\n");
	fprintf (stderr, "  Disallow ext. buffer control       --norbc\n");
	fprintf (stderr, "  Update info every nth cycle   (99) --update <integer>\n");
	fprintf (stderr, "  Limit processing count             --limit  <integer>\n");
	fprintf (stderr, "  Don't display running info         --quiet\n");
	fprintf (stderr, "  Don't output anything on std*      --shutup\n");
	fprintf (stderr, "  Enable Remote Control / GUI        --io\n");
	fprintf (stderr, "     Disable push to GUI             --nopush\n");
	fprintf (stderr, "     GUI host            (localhost) --iohost <string>\n");
	fprintf (stderr, "     GUI port(UDP)           (20220) --ioport <string>\n");
	fprintf (stderr, "  Quit on incompatibility            --close\n");
//      fprintf (stderr, "  Use TCP instead of UDP       (UDP) --tcp    <integer>\n");
//still borked
//to test: --tcp (port of remote tcp host)
	fprintf (stderr, "listening_port:   <integer>\n\n");
	fprintf (stderr, "If listening_port==0: use random port\n");
	fprintf (stderr, "Example: jack_audio_receive --out 8 --connect --pre 200 1234\n");
	fprintf (stderr, "One message corresponds to one multi-channel (mc) period.\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools\n\n");
	exit (0);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",        no_argument,            0, 'h'},
	{"version",     no_argument,            0, 'v'},
	{"loinfo",      no_argument,            0, 'x'},
	{"out",         required_argument,      0, 'o'},
	{"offset",      required_argument,      0, 'f'},
	{"connect",     no_argument,    &autoconnect, 1},
	{"16",          no_argument,            0, 'y'},
	{"name",        required_argument,      0, 'n'},
	{"sname",       required_argument,      0, 's'},
	{"pre",         required_argument,      0, 'b'},//pre (delay before playback) buffer
	{"max",         required_argument,      0, 'm'},//max (allocate) buffer
	{"rere",        no_argument,    &rebuffer_on_restart, 1},
	{"reuf",        no_argument,    &rebuffer_on_underflow, 1},
	{"nozero",      no_argument,    &zero_on_underflow, 0},
	{"norbc",       no_argument,    &allow_remote_buffer_control, 0},
	{"update",      required_argument,      0, 'u'},//screen info update every nth cycle
	{"limit",       required_argument,      0, 'l'},//test, stop after n processed
	{"close",       no_argument,    &close_on_incomp, 1},//close client rather than telling sender to stop
	{"quiet",       no_argument,    &quiet, 1},
	{"shutup",      no_argument,    &shutup, 1},
	{"io",          no_argument,    &io_enabled, 1},
	{"nopush",      no_argument,    &io_push_enabled, 0},
	{"iohost",      required_argument,      0, 'a'},
	{"ioport",      required_argument,      0, 'c'},
	{"tcp",         required_argument,      0, 't'}, //server port of remote host
	{0, 0, 0, 0}
};

//================================================================
//this order should reflect the same as in the .c file

//main start point
int main (int argc, char *argv[]);

//main audio process cycle driven by JACK
int process(jack_nframes_t nframes, void *arg);

void print_info();

//register messages to listen to
void registerOSCMessagePatterns(const char *port);


int osc_offer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_audio_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_buffer_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

//called by lo_server when OSC was invalid / network port already used / ..
void osc_error_handler(int num, const char *msg, const char *path);

//ctrl+c etc
static void signal_handler(int sig);

//send config to osc gui
void io_dump_config();

#endif //JACK_AUDIO_RECEIVE_H_INCLUDED
