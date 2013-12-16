#ifndef JACK_AUDIO_COMMON_H_INCLUDED
#define JACK_AUDIO_COMMON_H_INCLUDED

//jack_audio_common.h

extern float version;

extern lo_server_thread lo_st;

extern jack_client_t *client;

extern jack_port_t **ioPortArray;

extern jack_default_audio_sample_t **ioBufferArray;

extern int sample_rate;
extern int period_size;
extern int bytes_per_sample;

extern int autoconnect;

extern int max_channel_count;

extern int shutdown_in_progress;

extern uint64_t buffer_overflow_counter;

extern int update_display_every_nth_cycle;
extern int relaxed_display_counter;

extern int test_mode;

int last_test_cycle;

extern frames_since_cycle_start;
extern frames_since_cycle_start_sum;
extern frames_since_cycle_start_avg;

extern fscs_avg_calc_interval;

extern fscs_avg_counter;

extern struct timeval tv;

extern int process_enabled;

//=======

uint64_t get_free_mem(void);

void print_header(char *prgname);

void periods_to_HMS(char *buf,uint64_t periods);

void format_seconds(char *buf, float seconds); 

void read_jack_properties();

void print_common_jack_properties();

#endif //JACK_AUDIO_COMMON_H_INCLUDED
