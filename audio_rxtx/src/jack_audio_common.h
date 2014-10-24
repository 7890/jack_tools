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

#include "weak_libjack.h"

#ifndef JACK_AUDIO_COMMON_H_INCLUDED
#define JACK_AUDIO_COMMON_H_INCLUDED

//jack_audio_common.h

#define MIN_(a,b) ( ((a) < (b)) ? (a) : (b) )
#define MAX_(a,b) ( ((a) > (b)) ? (a) : (b) )

//recompilation needed when liblo is updated

#ifndef LO_MAX_UDP_MSG_SIZE
#define LO_MAX_UDP_MSG_SIZE LO_MAX_MSG_SIZE
#endif
#ifndef LO_DEFAULT_MAX_MSG_SIZE
#define LO_DEFAULT_MAX_MSG_SIZE LO_MAX_MSG_SIZE
#endif

#ifdef _WIN
#define PRId64 "llu"
#endif

extern float version;
extern float format_version;

extern lo_server_thread lo_st;

extern const char *server_name;
extern const char *client_name;

extern jack_client_t *client;

extern jack_port_t **ioPortArray;

extern jack_default_audio_sample_t **ioBufferArray;

extern int sample_rate;
extern int period_size;
extern int bytes_per_sample;

extern int autoconnect;

extern int max_channel_count;

jack_options_t jack_opts;

extern int shutdown_in_progress;

extern uint64_t buffer_overflow_counter;

extern int update_display_every_nth_cycle;
extern int relaxed_display_counter;

extern int test_mode;

extern int last_test_cycle;

extern int frames_since_cycle_start;
extern int frames_since_cycle_start_sum;
extern int frames_since_cycle_start_avg;

extern int fscs_avg_calc_interval;

extern int fscs_avg_counter;

extern int process_enabled;

void print_header(char *prgname);
void print_bytes_per_sample();

void print_version();

void periods_to_HMS(char *buf,uint64_t periods);

void format_seconds(char *buf, float seconds); 

void read_jack_properties();

void print_common_jack_properties();

int check_lo_props();

int io_();
void io_simple(char *path);
void io_simple_string(char *path, const char *string);
void io_simple_string_double(char *path, const char *string1, const char *string2);
void io_simple_int(char *path, int i);
void io_simple_long(char *path, uint64_t l);
void io_simple_float(char *path, float);

void io_quit(char *token);

void jack_shutdown_handler (void *arg);

int xrun_handler();

extern int quiet;
extern int shutup;

extern int io_enabled;
extern int io_push_enabled;

extern char* io_host;
extern char* io_port;

extern lo_address loio;

extern uint64_t local_xrun_counter;

#endif //JACK_AUDIO_COMMON_H_INCLUDED
