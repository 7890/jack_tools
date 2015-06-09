/*
  (c) Kjetil Matheussen, 2005/2006.
  (c) Jonathan Woithe, 2006.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

//tb/150608

#include "weak_libjack.h"

#ifndef JACK_PLAY_H_INCLUDED
#define JACK_PLAY_H_INCLUDED

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sndfile.h>
#include <pthread.h>
#include <math.h>

//#include <jack/jack.h>
//#include <jack/ringbuffer.h>

#define JC_MAX(a,b) (((a)>(b))?(a):(b))
#define JC_MIN(a,b) (((a)<(b))?(a):(b))

#define OPTARGS_BEGIN(das_usage) {int lokke;const char *usage=das_usage;for(lokke=1;lokke<argc;lokke++){char *a=argv[lokke];if(!strcmp("--help",a)||!strcmp("-h",a)){printf(usage);return 0;
#define OPTARG(name,name2) }}else if(!strcmp(name,a)||!strcmp(name2,a)){{
#define OPTARG_GETINT() atoi(argv[++lokke])
#define OPTARG_GETFLOAT() atof(argv[++lokke])
#define OPTARG_GETSTRING() argv[++lokke]
#define OPTARG_LAST() }}else if(lokke==argc-1){lokke--;{
#define OPTARGS_ELSE() }else if(1){
#define OPTARGS_END }else{fprintf(stderr,usage);return(-1);}}}


typedef jack_default_audio_sample_t sample_t;
struct ringbuffer_block
{
        sample_t *buffer;
};

//////////////////////// BUFFERS ////////////////////////////////////
static void buffers_init(int jackbuffersize);
static sample_t *buffer_get(void);
int buffersizecallback(size_t newsize,void *arg);

//////////////////////// PORTNAMES //////////////////////////////////
static int findnumports(char **ports);
static void portnames_add_defaults(void);
static void portnames_add(char *name);
static char **portnames_get_connections(int ch);

//////////////////////// DISK ///////////////////////////////////////
static int disk_read(SNDFILE *soundfile,sample_t *buffer,size_t frames);
static void *disk_thread_func (void *arg);
void setup_disk_thread (void);
void stop_disk_thread(void);

//////////////////////// JACK ///////////////////////////////////////
void put_buffer_into_jack_buffers(sample_t *buffer);
void req_buffer_from_disk_thread();
int process (jack_nframes_t nframes, void *arg);

/////////////////// JACK CONNECTIONS ////////////////////////////////

static int compaire(const void *a, const void *b);
static int reconnect_ports_questionmark(void);
static void disconnect_ports(void);
static void connect_ports(void);
static void* connection_thread(void *arg);
static void wake_up_connection_thread(void);
static void start_connection_thread(void);
static void stop_connection_thread(void);
static int graphordercallback(void *arg);
static void create_ports(void);

/////////////////// INIT / SHUTDOWN /////////////////////////////////

static void do_exit(int close_jack);
static void finish(int sig);
static void jack_shutdown(void *arg);
static jack_client_t *new_jack_client(char *name);
static void start_jack(void);

/////////////////// MAIN ////////////////////////////////////////////

int main (int argc, char *argv[]);

#endif //JACK_PLAY_H_INCLUDED
