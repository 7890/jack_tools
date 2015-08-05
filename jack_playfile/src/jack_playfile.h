#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <getopt.h>

#ifndef WIN32
	#include <termios.h>
#endif

#ifndef PRId64
	#define PRId64 "llu"
#endif

#include "weak_libjack.h"

#include <sndfile.h>
#include <zita-resampler/resampler.h>
#include <opusfile.h>
#include <vorbis/vorbisfile.h>

//should be optional
#include <mpg123.h>

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

#ifdef WIN32
	#include <windows.h>
	#define bzero(p, l) memset(p, 0, l)
#endif

typedef jack_default_audio_sample_t sample_t;

typedef struct
{ sf_count_t  frames ; //Used to be called samples.
  int         samplerate ;
  int         channels ;
  int         format ;
} SF_INFO_GENERIC ;

#define SF_FORMAT_OPUS 0x900000
#define SF_FORMAT_MP3 0x910000

//tb/150612

//simple file player for JACK
//inspired by jack_play, libsndfile, zresampler

static const float version=0.8;

//================================================================
int main(int argc, char *argv[]);
static void handle_key_hits();
static void print_keyboard_shortcuts();

static int process(jack_nframes_t nframes, void *arg);

static void seek_frames_absolute(int64_t frames_abs);
static void seek_frames(int64_t frames_rel);

static int get_resampler_pad_size_start();
static int get_resampler_pad_size_end();

static void setup_resampler();

static void resample();
static void deinterleave();

static int disk_read_frames();
static void *disk_thread_func(void *arg);
static void setup_disk_thread();
static void req_buffer_from_disk_thread();

static void free_ringbuffers();
static void reset_ringbuffers();

static void signal_handler(int sig);
static void jack_shutdown_handler (void *arg);

static void fill_jack_output_buffers_zero();
static void set_seconds_from_exponent();
static void set_frames_from_exponent();
static void increment_seek_step_size();
static void decrement_seek_step_size();
static void seek_frames_absolute(int64_t frames_abs);
static void seek_frames(int64_t frames_rel);

static double frames_to_seconds(sf_count_t frames, int samplerate);
static double get_seconds(SF_INFO_GENERIC *sf_info);
static const char * format_duration_str(double seconds);
static const char * generate_duration_str(SF_INFO_GENERIC *sf_info);
static int file_info(SF_INFO_GENERIC sf_info, int print);
static sf_count_t sf_seek_(sf_count_t offset, int whence);
static void sf_close_();

static void print_stats();

static void reset_terminal();
static void init_term_seq();
static void set_terminal_raw();
static int read_raw_key();
static void init_key_codes();

static void print_next_wheel_state(int direction);
static void print_clock();

//for comments on these variables, see jack_playfile.c
extern const char *server_name;
extern const char *client_name;

extern uint64_t frame_offset;
extern uint64_t frame_count;
extern int keyboard_control_enabled;
extern int use_resampling;
extern int autoconnect_jack_ports;
extern int try_jack_reconnect;
extern int connect_to_sisco;
extern int pause_when_finished;
extern int is_playing;
extern int is_muted;
extern int loop_enabled;
extern int is_time_seconds;
extern int is_time_absolute;
extern int is_time_elapsed;
extern int is_clock_displayed;

//================================================================
static void print_main_help (void)
{
	fprintf (stderr, "Usage: jack_playfile [Options] audiofile\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text and quit          --help\n");
	fprintf (stderr, "  Show program version and quit       --version\n");
	fprintf (stderr, "  JACK client name    (jack_playfile) --name <string>\n");
	fprintf (stderr, "  JACK server name          (default) --sname <string>\n");
	fprintf (stderr, "  Don't connect JACK ports            --noconnect\n");
	fprintf (stderr, "  Don't wait for JACK to re-connect   --noreconnect\n");
	fprintf (stderr, "  Disable keyboard control            --nocontrol\n");
	fprintf (stderr, "  Disable resampling                  --noresampling\n");
	fprintf (stderr, "  Start paused                        --paused\n");
	fprintf (stderr, "  Start muted                         --muted\n");
	fprintf (stderr, "  Enable loop                         --loop\n");
	fprintf (stderr, "  Show time as frames       (seconds) --frames\n");
	fprintf (stderr, "  Show absolute time            (rel) --absolute\n");
	fprintf (stderr, "  Show remaining time       (elapsed) --remaining\n");
	fprintf (stderr, "  Disable clock display               --noclock\n");
	fprintf (stderr, "  Frame offset:                   (0) --offset <integer>\n");
	fprintf (stderr, "  Frame count:                  (all) --count <integer>\n\n");

	fprintf (stderr, "Example: jack_playfile --remaining --count 44100 --loop music.opus\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools/\n\n");

	fprintf (stderr, "jack_playfile is free software.\n");
	fprintf (stderr, "Major audio libraries jack_playfile depends on:\n\n");

	fprintf (stderr, "JACK audio connection kit - http://jackaudio.org/\n");
	fprintf (stderr, "libsndfile - http://www.mega-nerd.com/libsndfile/\n");
	fprintf (stderr, "libzita-resampler - http://kokkinizita.linuxaudio.org/linuxaudio/\n");
	fprintf (stderr, "libopus, libopusfile - http://www.opus-codec.org/\n");
	fprintf (stderr, "libvorbisfile - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libmpg123 - http://www.mpg123.de/ (optional due to patent foo)\n\n");

	fprintf (stderr, "libraries abstracted by libsndfile:\n\n");
	fprintf (stderr, "libFLAC - http://xiph.org/flac/\n");
	fprintf (stderr, "libvorbis, libvorbisenc - http://xiph.org/vorbis/\n");
	fprintf (stderr, "libogg - http://xiph.org/ogg/\n\n");

	exit (0);
}

//data structure for command line options parsing
//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
//================================================================
static struct option long_options[] =
{
	{"help",	no_argument,		0, 'a'},
	{"version",	no_argument,		0, 'b'},
	{"name",	required_argument,	0, 'c'},
	{"sname",	required_argument,	0, 'd'},

	{"offset",	required_argument,	0, 'e'},
	{"count",	required_argument,	0, 'f'},

	{"nocontrol",	no_argument,  &keyboard_control_enabled,0},
	{"noresampling",no_argument,  &use_resampling,		0},
	{"noconnect",	no_argument,  &autoconnect_jack_ports,	0},
	{"noreconnect",	no_argument,  &try_jack_reconnect,	0},
	{"paused",	no_argument,  &is_playing,		0},
	{"muted",	no_argument,  &is_muted,		1},
	{"loop",	no_argument,  &loop_enabled,		1},
	{"frames",	no_argument,  &is_time_seconds,		0},
	{"absolute",	no_argument,  &is_time_absolute,	1},
	{"remaining",	no_argument,  &is_time_elapsed,		0},
	{"noclock",	no_argument,  &is_clock_displayed,	0},
//{"",  no_argument,  &connect_to_sisco, 0},
//{"",  no_argument,  &pause_when_finished, 1},
	{0, 0, 0, 0}
};

//=========================================================
void print_header()
{
	fprintf (stderr, "\njack_playfile v%.2f\n", version);
	fprintf (stderr, "(C) 2015 Thomas Brand  <tom@trellis.ch>\n");
}

//=========================================================
void print_version()
{
	fprintf (stderr, "%.2f\n",version);
}

//for displaying 'wheel' as progress indicator
static int wheel_state=0;
//=============================================================================
//-1: counter clockwise 1: clockwise
static void print_next_wheel_state(int direction)
{
	wheel_state+=direction;

	if(wheel_state>5)
	{
		wheel_state=0;
	}
	else if(wheel_state<0)
	{
		wheel_state=5;
	}

	if(wheel_state==0)
	{
		fprintf(stderr,"(.    ");
	}
	if(wheel_state==1)
	{
		fprintf(stderr,"(`    ");
	}
	if(wheel_state==2)
	{
		fprintf(stderr," ``   ");
	}
	if(wheel_state==3)
	{
		fprintf(stderr,"  `)  ");
	}
	if(wheel_state==4)
	{
		fprintf(stderr,"  .)  ");
	}
	if(wheel_state==5)
	{
		fprintf(stderr," ..   ");
	}
}

//=============================================================================
static double frames_to_seconds(sf_count_t frames, int samplerate)
{
	double seconds;
	if (frames==0)
	{
		return 0;
	}
	seconds = (1.0 * frames) / samplerate;
	return seconds;
}

//=============================================================================
static double get_seconds(SF_INFO_GENERIC *sf_info)
{
	double seconds;
	if (sf_info->samplerate < 1)
	{
		return 0;
	}
	if (sf_info->frames / sf_info->samplerate > 0x7FFFFFFF)
	{
		return -1;
	}
	seconds = (1.0 * sf_info->frames) / sf_info->samplerate;
	return seconds;
}

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char * format_duration_str(double seconds)
{
	static char str [128] ;
	int hrs, min ;
	double sec ;
	memset (str, 0, sizeof (str)) ;
	hrs = (int) (seconds / 3600.0) ;
	min = (int) ((seconds - (hrs * 3600.0)) / 60.0) ;
	sec = seconds - (hrs * 3600.0) - (min * 60.0) ;
	snprintf (str, sizeof (str) - 1, "%02d:%02d:%06.3f", hrs, min, sec) ;
	return str ;
}

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char * generate_duration_str(SF_INFO_GENERIC *sf_info)
{
	return(
		format_duration_str(
			get_seconds(sf_info)
		)
	);
}

//=============================================================================
static int is_flac(SF_INFO_GENERIC sf_info)
{
	if( (sf_info.format & SF_FORMAT_TYPEMASK)==SF_FORMAT_FLAC )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

//=============================================================================
static int is_ogg(SF_INFO_GENERIC sf_info)
{
	if( (sf_info.format & SF_FORMAT_TYPEMASK)==SF_FORMAT_OGG )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

//=============================================================================
static int file_info(SF_INFO_GENERIC sf_info, int print)
{
	/*
	SF_FORMAT_SUBMASK               = 0x0000FFFF
	SF_FORMAT_TYPEMASK              = 0x0FFF0000
	*/
	int bytes=0;

	char* format_string;

	switch (sf_info.format & SF_FORMAT_TYPEMASK)
	{
	//http://www.mega-nerd.com/libsndfile/api.html
	//major formats
	case SF_FORMAT_WAV 	:format_string="Microsoft WAV format (little endian)"; break;
	case SF_FORMAT_AIFF 	:format_string="Apple/SGI AIFF format (big endian)"; break;
	case SF_FORMAT_AU 	:format_string="Sun/NeXT AU format (big endian)"; break;
	case SF_FORMAT_RAW 	:format_string="RAW PCM data"; break;
	case SF_FORMAT_PAF 	:format_string="Ensoniq PARIS file format"; break;
	case SF_FORMAT_SVX 	:format_string="Amiga IFF / SVX8 / SV16 format"; break;
	case SF_FORMAT_NIST 	:format_string="Sphere NIST format"; break;
	case SF_FORMAT_VOC 	:format_string="VOC files"; break;
	case SF_FORMAT_IRCAM 	:format_string="Berkeley/IRCAM/CARL"; break;
	case SF_FORMAT_W64 	:format_string="Sonic Foundry's 64 bit RIFF/WAV"; break;
	case SF_FORMAT_MAT4 	:format_string="Matlab (tm) V4.2 / GNU Octave 2.0"; break;
	case SF_FORMAT_MAT5  	:format_string="Matlab (tm) V5.0 / GNU Octave 2.1"; break;
	case SF_FORMAT_PVF 	:format_string="Portable Voice Format"; break;
	case SF_FORMAT_XI 	:format_string="Fasttracker 2 Extended Instrument"; break;
	case SF_FORMAT_HTK 	:format_string="HMM Tool Kit format"; break;
	case SF_FORMAT_SDS 	:format_string="Midi Sample Dump Standard"; break;
	case SF_FORMAT_AVR 	:format_string="Audio Visual Research"; break;
	case SF_FORMAT_WAVEX 	:format_string="MS WAVE with WAVEFORMATEX"; break;
	case SF_FORMAT_SD2 	:format_string="Sound Designer 2"; break;
	case SF_FORMAT_FLAC 	:format_string="FLAC lossless file format"; break;
	case SF_FORMAT_CAF 	:format_string="Core Audio File format"; break;
	case SF_FORMAT_WVE 	:format_string="Psion WVE format"; break;
	case SF_FORMAT_OGG 	:format_string="Xiph OGG container"; break;
	case SF_FORMAT_MPC2K 	:format_string="Akai MPC 2000 sampler"; break;
	case SF_FORMAT_RF64 	:format_string="RF64 WAV file"; bytes=8; break;
	case SF_FORMAT_OPUS 	:format_string="Opus (RFC6716)"; break;
	case SF_FORMAT_MP3 	:format_string="MPEG Layer 3 (mp3)"; break;

	default :
		format_string="unknown format!";
		break ;
	};

	char* sub_format_string;

	switch (sf_info.format & SF_FORMAT_SUBMASK)
	{
	//subtypes from here on
	case SF_FORMAT_PCM_S8       : sub_format_string="Signed 8 bit data"; bytes=1; break;
	case SF_FORMAT_PCM_16       : sub_format_string="Signed 16 bit data"; bytes=2; break;
	case SF_FORMAT_PCM_24       : sub_format_string="Signed 24 bit data"; bytes=3; break;
	case SF_FORMAT_PCM_32       : sub_format_string="Signed 32 bit data"; bytes=4; break;

	case SF_FORMAT_PCM_U8       : sub_format_string="Unsigned 8 bit data (WAV and RAW only)"; bytes=1; break;

	case SF_FORMAT_FLOAT	    : sub_format_string="32 bit float data"; bytes=4; break;
	case SF_FORMAT_DOUBLE       : sub_format_string="64 bit float data"; bytes=8; break;

	case SF_FORMAT_ULAW         : sub_format_string="U-Law encoded"; break;
	case SF_FORMAT_ALAW         : sub_format_string="A-Law encoded"; break;
	case SF_FORMAT_IMA_ADPCM    : sub_format_string="IMA ADPCM"; break;
	case SF_FORMAT_MS_ADPCM     : sub_format_string="Microsoft ADPCM"; break;

	case SF_FORMAT_GSM610       : sub_format_string="GSM 6.10 encoding"; break;
	case SF_FORMAT_VOX_ADPCM    : sub_format_string="Oki Dialogic ADPCM encoding"; break;

	case SF_FORMAT_G721_32      : sub_format_string="32kbs G721 ADPCM encoding"; break;
	case SF_FORMAT_G723_24      : sub_format_string="24kbs G723 ADPCM encoding"; break;
	case SF_FORMAT_G723_40      : sub_format_string="40kbs G723 ADPCM encoding"; break;

	case SF_FORMAT_DWVW_12      : sub_format_string="12 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_16      : sub_format_string="16 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_24      : sub_format_string="24 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_N       : sub_format_string="N bit Delta Width Variable Word encoding"; break;

	case SF_FORMAT_DPCM_8       : sub_format_string="8 bit differential PCM (XI only)"; break;
	case SF_FORMAT_DPCM_16      : sub_format_string="16 bit differential PCM (XI only)"; break;

	case SF_FORMAT_VORBIS       : sub_format_string="Xiph Vorbis encoding"; break;
	default :
		sub_format_string="unknown subformat!";
		break ;
	};

	const char *duration_str;
	duration_str=generate_duration_str(&sf_info);

	if(print)
	{
		fprintf(stderr,"format:      %s\n	     %s (0x%08X)\nduration:    %s (%"PRId64" frames)\nsamplerate:  %d\nchannels:    %d\n"
			,format_string, sub_format_string, sf_info.format
			,duration_str
			,sf_info.frames
			,sf_info.samplerate
			,sf_info.channels
		);
	}

	return bytes;
}//end print_file_info
//EOF
