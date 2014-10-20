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

#ifndef JACK_AUDIO_SEND_H_INCLUDED
#define JACK_AUDIO_SEND_H_INCLUDED

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

int nopause=0; //param

//================================================================
static void print_help (void)
{
	fprintf (stderr, "Usage: jack_audio_send [Options] target_host target_port.\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text and quit          --help\n");
	fprintf (stderr, "  Show program version and quit       --version\n");
	fprintf (stderr, "  Show liblo properties and quit      --loinfo\n");
	fprintf (stderr, "  Local port                   (9990) --lport  <integer>\n");
	fprintf (stderr, "  Number of capture channels      (2) --in     <integer>\n");
	fprintf (stderr, "  Autoconnect ports                   --connect\n");
	fprintf (stderr, "  Send 16 bit samples  (32 bit float) --16\n");
	fprintf (stderr, "  JACK client name             (send) --name   <string>\n");
	fprintf (stderr, "  JACK server name          (default) --sname  <string>\n");
	fprintf (stderr, "  Update info every nth cycle    (99) --update <integer>\n");
	fprintf (stderr, "  Limit totally sent messages         --limit  <integer>\n");
	fprintf (stderr, "  Don't display running info          --quiet\n");
	fprintf (stderr, "  Don't output anything on std*       --shutup\n");
	fprintf (stderr, "  Enable Remote Control / GUI         --io\n");
	fprintf (stderr, "     Disable push to GUI              --nopush\n");
	fprintf (stderr, "     GUI host             (localhost) --iohost <string>\n");
	fprintf (stderr, "     GUI port(UDP)            (20220) --ioport <string>\n");
	fprintf (stderr, "  Immediate send, ignore /pause       --nopause\n");
	fprintf (stderr, "  (Use with multiple receivers. Ignore /pause, /deny)\n");
	fprintf (stderr, "  Drop every nth message (test)   (0) --drop   <integer>\n");
	fprintf (stderr, "target_host:   <string>\n");
	fprintf (stderr, "target_port:   <integer>\n\n");
	fprintf (stderr, "If target_port==0 and/or --lport 0: use random port(s)\n");
	fprintf (stderr, "Example: jack_audio_send --in 8 10.10.10.3 1234\n");
	fprintf (stderr, "One message corresponds to one multi-channel (mc) period.\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools/\n\n");
        exit (0);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",        no_argument,            0, 'a'},
	{"version",     no_argument,            0, 'b'},
	{"loinfo",      no_argument,            0, 'c'},
	{"lport",       required_argument,      0, 'd'},
	{"in",          required_argument,      0, 'e'},
	{"connect",     no_argument,    &autoconnect, 1},//done
	{"16",          no_argument,            0, 'f'},
	{"name",        required_argument,      0, 'g'},
	{"sname",       required_argument,      0, 'h'},
	{"update",      required_argument,      0, 'i'},
	{"limit",       required_argument,      0, 'j'},
	{"quiet",       no_argument,    &quiet, 1},//done
	{"shutup",      no_argument,    &shutup, 1},//done
	{"io",          no_argument,    &io_enabled, 1},//done
	{"nopush",      no_argument,    &io_push_enabled, 0},//done
	{"iohost",      required_argument,      0, 'k'},
	{"ioport",      required_argument,      0, 'l'},
	{"nopause",     no_argument,    &nopause, 1},//done
	{"drop",        required_argument,      0, 'm'},
	{0, 0, 0, 0}
};

//================================================================
//this order should reflect the same as in the .c file

//main start point
int main (int argc, char *argv[]);

//main audio process cycle driven by JACK
int process(jack_nframes_t nframes, void *arg);

void offer_audio_to_receiver();

//register messages to listen to
void registerOSCMessagePatterns(const char *port);

int osc_accept_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_deny_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_pause_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

int osc_quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data);

//called by lo_server when OSC was invalid / network port already used / ..
void osc_error_handler(int num, const char *msg, const char *path);

//ctrl+c etc
static void signal_handler(int sig);

//send config to osc gui
void io_dump_config();

//create a dummy message, return size in bytes (message length)
//don't forget to update when changing the real message (format) in process()
int message_size();

#endif //JACK_AUDIO_SEND_H_INCLUDED
