//#define HAS_JACK_METADATA_API
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <jack/jack.h>
#include "meta/jackey.h"
#include "meta/jack_osc.h"
#include <lo/lo.h>
//#include <regex.h>
#include <math.h>

//tb/140511/140513
//test tone signal generator
//control via jack osc (use jack_osc_bridge_in or similar)
//jack1
//gcc -o jack_gen gen.c -DHAS_JACK_METADATA_API `pkg-config --libs jack liblo` -lm
//jack2
//gcc -o jack_gen gen.c `pkg-config --libs jack liblo` -lm

/*
urls of interest:
http://en.wikipedia.org/wiki/Note
http://en.wikipedia.org/wiki/Equal_temperament
http://www.phy.mtu.edu/~suits/notefreqs.html

signal generator
================

given (jack):
	sampling rate
	period size (cycle buffer size)
	jack transport state

(default bpm = 120*)

info:
	/print/info

shapes:
	/shape/sinus
	/shape/rectangle
		/gen/pulse_length f 	#(high) >=0, <= shape period length
		/gen/pulse_length_ratio f #(high) relative to shape period length >=0, <=1
		/gen/pulse_length_auto	#set pulse length (high) to half of shape period size
		/gen/pulse_length_auto i	#set automatic pulse length setting (0=off,1=on).
						#if on, will recalc pulse length on frequency change

frequency / periodicity:
	/freq/herz f
	/freq/bpm f
	/freq/samples f
	/freq/nth_cycle f	#f times size of jack cycle
	/freq/wavelength f	#[mm]
		/freq/speed_of_sound f	#[m/s] (345*)
	/freq/period_duration	#[ms]
	/freq/multiply f	#multiply current freq with f

#add or substract f from freq in given units
	/freq/herz/rel f	
	/freq/bpm/rel f
	/freq/samples/rel f
	/freq/nth_cycle/rel f
#	/freq/wavelength/rel f		#[mm]
	/freq/period_duration/rel	#[ms]


	/freq/a4_ref f		#[hz] A4 reference for chromatic midi freqs (440*)
	/freq/midi_note i	#(>=0, <=127, C-1=0, A4=69, G9=127)
	/freq/midi_note s	#C-1 - G9, upper- and lower case possible
				#midi note symbol is a composition of
				#starting with one of: C,C#,D,D#,E,F,F#,G,G#,A,A#,B
				#followed by octave: -1 - 9


	/freq/midi_note/rel i

basic control:
	/gen/on			#global on: fill audio output buffers with generated signal
	/gen/off		#global off: fill audio output buffers with 0 (off) or with generated signal (on)
	/gen/restart_at i	#restart signal at jack cycle __sample__ position i (stop currently ongoing)
	/gen/stop		#stop (act like if duration was elapsed but do not loop)

#	/gen/restart_on_freq_change i	#automatically restart signal on freq change 0: off 1: on
#	/gen/restart_on_pulse_length_change i	#automatically restart signal on pulse length change 0: off 1: on

	/gen/loop_enable i	#0: off 1: on
	/gen/loop_gap i		# __samples__ to wait before looping after duration elapsed

minimal processing:
	/gen/amplify f		#multiplication, linear (-1 to invert)
	/gen/dc_offset f	#addition
	/gen/limit_high f	#clamp samples with value > f to f
	/gen/limit_low f	#clamp samples with value < f to f

duration of signal output:
	/duration/inf		#infinite
	/duration/beats f		#current bpm used. default bpm is 120
	/duration/samples i
	/duration/nth_cycle f		#f times size of jack cycle
	/duration/time f		#[ms]
	/duration/follow_transport i	#0: off, 1: on -> generate signal only while jack transport is rolling


note: osc input values are not validated

*/

//osc
static jack_port_t* port_in;
//audio
static jack_port_t* port_out;

jack_client_t* client;
char const client_name[32] = "gen";

//osc
void* buffer_in;
//audio
jack_default_audio_sample_t* buffer_out;

int msgCount=0;
char* path;
char* types;

jack_position_t pos;
jack_transport_state_t tstate;

#ifdef HAS_JACK_METADATA_API
jack_uuid_t osc_port_uuid;
#endif

//==================================================================
void exit(int err);

void setup();

double herz_to_samples(double herz);
double samples_to_herz(double samples);

double bpm_to_samples(double bpm);
double samples_to_bpm(double samples);

double beats_to_samples(double beats);
double samples_to_beats(double samples);

double nth_cycle_to_samples(double nth_cycle);
double samples_to_nth_cycle(double samples);

double wavelength_to_samples(double wavelength);
double wavelength_to_herz(double wavelength);
double samples_to_wavelength(double samples);

double time_to_samples(double duration);
double samples_to_time(double samples);

double midi_note_to_samples(int midi_note_number);
double midi_note_symbol_to_samples(char* midi_note_symbol);

double midi_note_to_herz(int midi_note_number);
double herz_to_wavelength(double frequency);

void set_pulse_length_auto();

void set_all_freq(double samples);
void set_all_duration(double samples);

void print_all_properties();

void create_midi_notes();
void update_midi_notes();
void clear_buffer(jack_nframes_t frames,jack_default_audio_sample_t* buff);
static void signal_handler(int sig);

int find_nearest_midi_note(float samples);

//==================================================================

struct JackProps
{
	int sampling_rate;
	int samples_per_period;
	int transport_state; //0: stopped, 1: started
}; 
struct JackProps jack;

//==================================================================

//shape types
#define SHAPE_SINUS 0
#define SHAPE_RECTANGLE 1

//status types
#define STATUS_OFF 0
#define STATUS_ON 1

struct Generator
{
	int status;

	int shape; 
	double pulse_length; //[samples] 0: all low, shape period length: all high
	int pulse_length_auto; //automatically recalc pulse length when frequency changes

	double current_sample_in_period;
	double low;
	int current_pulse_length;

	double radstep; //for sinus
	double current_rad;

	double amplification; //multiply (linear)
	double dc_offset; //add

	double limit_high;
	double limit_low;

	int restart_at; //sample number inside jack cycle
	int restart_pending;
	int stop_pending;
	int stopped;

	int loop_enabled;
	int loop_gap;
	int current_gap_sample;
	int loop_pending;

};
struct Generator gen;

//==================================================================

struct Duration
{
	int follow_transport; //0: off, 1: on -> generate signal only while jack transport is rolling
	int infinite; //0: no 1: yes

	double samples_elapsed;

	double samples;
	double beats; //for calculation of n beats duration, current freq / bmp used
	double nth_cycle; 
	double time; //[ms]
};
struct Duration dur;


//==================================================================

#define FREQ_DEF_SPEED_OF_SOUND 343 //[m/s] 0% humidity, 20 degrees celsius
#define FREQ_DEF_A4_REF 440 //[hz] room temperature

#define DEF_BPM 120 //[beat per minute]

struct Frequency
{
	double a4_ref; //[hz]
	double herz;
	double beats_per_minute;
	double samples_per_period; //shape period size (!= jack period size)
	double nth_cycle;
	double wavelength; //[mm] 
	double speed_of_sound; //[m/s]
	double period_duration; //[ms]
};
struct Frequency freq;

//==================================================================

//twelve-tone equal temperament, 12-TET
//http://en.wikipedia.org/wiki/Note
//The note-naming convention specifies a letter, any accidentals, and an octave number. 
//Any note is an integer of half-steps away from middle A (A4). Let this distance be denoted n. 
//If the note is above A4, then n is positive; if it is below A4, then n is negative. 
//The frequency of the note (f) (assuming equal temperament) is then:

//f = 2^(n/12) * 440hz

//The distance of an equally tempered semitone is divided into 100 cents. 
//So 1200 cents are equal to one octave — a frequency ratio of 2:1. 
//This means that a cent is precisely equal to the 1200th root of 2, 
//which is approximately 1.000578.

//p: midi note number

//p = 69 + 12 * log2(f/440hz)
//f = 2^((p-69)/12) * 440hz

struct MidiNote
{
	//midi note number given through position in array
	//(>=0, <=127, C-1=0, A4=69, G9=127)
	char symbol[16]; //(C-1 - G9)

	float samples;

};
struct MidiNote midi_notes[128];

//==================================================================
void minimal_test()
{
	//test
	printf("herz (441) to samples %f\n",herz_to_samples(441));
	printf("samples to herz %f\n",samples_to_herz(herz_to_samples(441)));

	printf("bpm (120) to samples %f\n",bpm_to_samples(120));
	printf("samples to bpm %f\n",samples_to_bpm(bpm_to_samples(120)));

	printf("nth cycle (-2) to samples %f\n",nth_cycle_to_samples(-2));
	printf("samples to nth cycle %f\n",samples_to_nth_cycle(nth_cycle_to_samples(-2)));

	printf("period duration (0.2267573696) to samples %f\n",time_to_samples(0.2267573696));
	printf("samples to period duration %f\n",samples_to_time(time_to_samples(0.2267573696)));

	//set_all_freq(wavelength_to_samples(0.123d)); 
	//set_all_freq(time_to_samples(1.0d));
	//set_all_freq(midi_note_to_samples(69));
	//set_all_freq(midi_note_symbol_to_samples("A4"));
	
	printf("beats (1) to samples %f\n",beats_to_samples(1));
	printf("samples to beats %f\n",samples_to_beats(beats_to_samples(1)));

	printf("\n");
}

//==================================================================

void setup()
{
	jack.sampling_rate=jack_get_sample_rate (client);
	jack.samples_per_period=jack_get_buffer_size (client);

	tstate = jack_transport_query (client, &pos);
	//will be updated in process()
	jack.transport_state=tstate;

	freq.speed_of_sound=FREQ_DEF_SPEED_OF_SOUND;
	freq.a4_ref=FREQ_DEF_A4_REF;

	create_midi_notes();

	//set_all_freq(herz_to_samples(441.0d));
	set_all_freq(herz_to_samples(jack.sampling_rate/100));
	//set_all_freq(bpm_to_samples(120.0d));
	//set_all_freq(44100.0d);
	//set_all_freq(nth_cycle_to_samples(-2));
	//set_all_freq(wavelength_to_samples(0.123d)); 
	//set_all_freq(time_to_samples(1.0d));
	//set_all_freq(midi_note_to_samples(69));
	//set_all_freq(midi_note_symbol_to_samples("A4"));

	//gen.shape=SHAPE_SINUS;
	gen.shape=SHAPE_RECTANGLE;

	gen.status=STATUS_ON;
	gen.amplification=1;
	gen.dc_offset=0;

	gen.limit_high=2;
	gen.limit_low=-2;

	gen.restart_at=0;
	gen.restart_pending=1;
	gen.stop_pending=0;
	gen.stopped=0;
	gen.loop_enabled=0;
	gen.loop_gap=22050;
	gen.current_gap_sample=0;
	gen.loop_pending=0;

	gen.pulse_length_auto=1;
	gen.pulse_length=(double)freq.samples_per_period/2;
	//gen.pulse_length=freq.samples_per_period-2;
	//gen.pulse_length=1;

	gen.current_sample_in_period=0;
	gen.low=0;
	gen.current_pulse_length=0;
	gen.radstep=(double)(2*M_PI)/freq.samples_per_period;
	gen.current_rad=0;

	dur.follow_transport=0; //0: off, 1: on -> generate signal only while jack transport is rolling
	dur.samples_elapsed=0;

	//0 means infinite
	set_all_duration(0);

}//end setup()

//==================================================================
//==================================================================
static int process (jack_nframes_t frames, void* arg)
{
//prepare receive buffer
	buffer_in = jack_port_get_buffer (port_in, frames);

//prepare send buffers
	buffer_out = jack_port_get_buffer(port_out, frames);

	clear_buffer(frames,buffer_out);

	tstate = jack_transport_query (client, &pos);
	jack.transport_state=tstate;

//process osc messages
	msgCount = jack_osc_get_event_count (buffer_in);

	int i;
	//iterate over encapsulated osc messages
	for (i = 0; i < msgCount; ++i) 
	{
		jack_osc_event_t event;
		int r;

		r = jack_osc_event_get (&event, buffer_in, i);
		if (r == 0)
		{
			//check if osc data, skip if other
			if(*event.buffer!='/'){continue;}

			path=lo_get_path(event.buffer,event.size);
			int result;
			lo_message msg = lo_message_deserialise(event.buffer, event.size, &result);
			types=lo_message_get_types(msg);

			lo_arg** args=lo_message_get_argv(msg);
			int argc=lo_message_get_argc(msg);

			if(argc==0 && !strcmp(path,"/print/info"))
			{
				print_all_properties();
			}
			//===
			else if(argc==0 && !strcmp(path,"/shape/sinus"))
			{
				gen.shape=SHAPE_SINUS;
			}
			else if(argc==0 && !strcmp(path,"/shape/rectangle"))
			{
				gen.shape=SHAPE_RECTANGLE;
			}
			//===
			else if(!strcmp(path,"/freq/samples") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(args[0]->f);
			}
			else if(!strcmp(path,"/freq/samples/rel") && !strcmp(types,"f") && args[0]->f != 0)
			{
				float sm=freq.samples_per_period+args[0]->f;
				set_all_freq(sm);
			}
			else if(!strcmp(path,"/freq/herz") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(herz_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/freq/herz/rel") && !strcmp(types,"f") && args[0]->f != 0)
			{
				float hz=freq.herz+args[0]->f;
				set_all_freq(herz_to_samples(hz));
			}
			else if(!strcmp(path,"/freq/bpm") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(bpm_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/freq/bpm/rel") && !strcmp(types,"f") && args[0]->f != 0)
			{
				float bpm=freq.beats_per_minute+args[0]->f;
				set_all_freq(bpm_to_samples(bpm));
			}
			else if(!strcmp(path,"/freq/nth_cycle") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(nth_cycle_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/freq/nth_cycle/rel") && !strcmp(types,"f") && args[0]->f != 0)
			{
				float nth=freq.nth_cycle+args[0]->f;
				set_all_freq(nth_cycle_to_samples(nth));
			}
			else if(!strcmp(path,"/freq/wavelength") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(wavelength_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/freq/period_duration") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(time_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/freq/period_duration/rel") && !strcmp(types,"f") && args[0]->f != 0)
			{
				float dur=freq.period_duration+args[0]->f;
				set_all_freq(time_to_samples(dur));
			}
			else if(!strcmp(path,"/freq/multiply") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_freq(freq.samples_per_period/args[0]->f);
			}
			else if(!strcmp(path,"/freq/a4_ref") && !strcmp(types,"f") && args[0]->f > 0)
			{
				freq.a4_ref=args[0]->f;
				update_midi_notes();
			}
			else if(!strcmp(path,"/freq/speed_of_sound") && !strcmp(types,"f") && args[0]->f > 0)
			{
				freq.speed_of_sound=args[0]->f;
				//recalc wavelength
				set_all_freq(freq.samples_per_period);
			}
			else if(!strcmp(path,"/freq/midi_note") && !strcmp(types,"i") && args[0]->i >= 0)
			{
				set_all_freq(midi_note_to_samples(args[0]->i));
			}
			else if(!strcmp(path,"/freq/midi_note/rel") && !strcmp(types,"i") && args[0]->i != 0)
			{
				int note=find_nearest_midi_note( freq.samples_per_period )
					+ args[0]->i;
				set_all_freq(midi_note_to_samples(note));
			}
			else if(!strcmp(path,"/freq/midi_note") && !strcmp(types,"s"))
			{
				set_all_freq(midi_note_symbol_to_samples(&args[0]->s));
			}
			//===
			// continue
			else if(argc==0 && !strcmp(path,"/gen/on"))
			{
				gen.status=STATUS_ON;
			}
			// pause
			else if(argc==0 && !strcmp(path,"/gen/off"))
			{
				gen.status=STATUS_OFF;
			}
			else if(!strcmp(path,"/gen/restart_at") && !strcmp(types,"i") && args[0]->i > 0)
			{
				gen.restart_at=args[0]->i;
				gen.restart_pending=1;
			}
			else if(argc==0 && !strcmp(path,"/gen/stop"))
			{
				gen.stop_pending=1;
			}
			else if(!strcmp(path,"/gen/loop_enable") && !strcmp(types,"i"))
			{
				if(args[0]->i == 1)
				{
					gen.loop_enabled=1;
				}
				else
				{
					gen.loop_enabled=0;
				}
			}
			else if(!strcmp(path,"/gen/loop_gap") && !strcmp(types,"i") && args[0]->i > 0)
			{
				gen.loop_gap=args[0]->i;
			}
			else if(!strcmp(path,"/gen/pulse_length") && !strcmp(types,"f") && args[0]->f >= 0)
			{
				gen.pulse_length=args[0]->f;

				if(gen.pulse_length>=freq.samples_per_period)
				{
					fprintf(stderr,"warning: pulse length is larger than or equally sized to shape period size!\n");
				}
				else if(gen.pulse_length==0)
				{
					fprintf(stderr,"warning: pulse length == 0!\n");
				}
			}
			else if(!strcmp(path,"/gen/pulse_length_ratio") && !strcmp(types,"f") && args[0]->f >= 0)
			{
				gen.pulse_length=freq.samples_per_period*args[0]->f;

				if(gen.pulse_length>=freq.samples_per_period)
				{
					fprintf(stderr,"warning: pulse length is larger than or equally sized to shape period size!\n");
				}
				else if(gen.pulse_length==0)
				{
					fprintf(stderr,"warning: pulse length == 0!\n");
				}
			}
			else if(argc==0 && !strcmp(path,"/gen/pulse_length_auto"))
			{
				set_pulse_length_auto();
			}
			else if(!strcmp(path,"/gen/pulse_length_auto") && !strcmp(types,"i"))
			{
				if(args[0]->i==1)
				{
					gen.pulse_length_auto=1;
					set_pulse_length_auto();
				}
				else
				{
					gen.pulse_length_auto=0;
				}
			}
			else if(!strcmp(path,"/gen/amplify") && !strcmp(types,"f"))
			{
				gen.amplification=args[0]->f;
			}
			else if(!strcmp(path,"/gen/dc_offset") && !strcmp(types,"f"))
			{
				gen.dc_offset=args[0]->f;
			}
			else if(!strcmp(path,"/gen/limit_high") && !strcmp(types,"f"))
			{
				gen.limit_high=args[0]->f;
			}
			else if(!strcmp(path,"/gen/limit_low") && !strcmp(types,"f"))
			{
				gen.limit_low=args[0]->f;
			}
			//===
			else if(argc==0 && !strcmp(path,"/duration/inf"))
			{
				set_all_duration(0);
			}
			else if(!strcmp(path,"/duration/samples") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_duration(args[0]->f);
			}
			else if(!strcmp(path,"/duration/beats") && !strcmp(types,"f")  && args[0]->f > 0)
			{
				set_all_duration(beats_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/duration/nth_cycle") && !strcmp(types,"f") && args[0]->f > 0)
			{
				set_all_duration(nth_cycle_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/duration/time") && !strcmp(types,"f") && args[0]->f > 0) //ms
			{
				set_all_duration(time_to_samples(args[0]->f));
			}
			else if(!strcmp(path,"/duration/follow_transport") && !strcmp(types,"i"))
			{
				if(args[0]->i==1)
				{
					dur.follow_transport=1;
				}
				else
				{
					dur.follow_transport=0;
				}
			}

			////////
			//cents
			//gloabl offset

			lo_message_free(msg);
		}
	} //end processing osc messages

//be silent and return under conditions
	if(
		gen.status==STATUS_OFF
		|| (dur.follow_transport==1 && jack.transport_state==0)
	)
	{
		return 0;
	}

//fill audio output buffer under conditions

	double val=0;

	for(i=0;i<frames;i++)
	{
		if(gen.restart_pending==1 && gen.restart_at==i)
		{
			gen.current_pulse_length=0;
			gen.current_sample_in_period=0;
			gen.low=0;
			gen.current_rad=0;
			dur.samples_elapsed=0;

			gen.stopped=0;
			gen.stop_pending=0;
			gen.restart_pending=0;
			gen.loop_pending=0;
			gen.current_gap_sample=0;
		}

		if
		(
			gen.stop_pending==1
			|| (dur.infinite==0 && dur.samples_elapsed > dur.samples)
		)
		{
			gen.current_pulse_length=0;
			gen.current_sample_in_period=0;
			gen.low=0;
			gen.current_rad=0;
			dur.samples_elapsed=0;

			if(gen.loop_enabled==1 && !gen.stop_pending==1)
			{
				gen.stopped=0;
				gen.loop_pending=1;
				//fprintf(stderr,"loop\n");
			}
			else
			{
				gen.stopped=1;
				gen.loop_pending=0;
				gen.current_gap_sample=0;
				//fprintf(stderr,"duration end\n");
			}
			gen.stop_pending=0;
		}

		if(gen.loop_pending==1 && gen.current_gap_sample <= gen.loop_gap)
		{
			//wait for loop_gap to be elapsed
			gen.current_gap_sample++;
			continue;
		}
		else if(gen.loop_pending==1 && gen.current_gap_sample > gen.loop_gap)
		{
			//prepare for restart
			gen.loop_pending=0;
			gen.current_gap_sample=0;
		}

		if(gen.stopped==1)
		{
			return 0;
		}

		if(dur.infinite==0)
		{
			dur.samples_elapsed++;
		}

		//========================================
		//main signal generation
		if(gen.shape==SHAPE_SINUS)
		{
			val=sin(gen.current_rad);
			gen.current_rad+=gen.radstep;
		}
		else if(gen.shape==SHAPE_RECTANGLE)
		{
			if(gen.current_sample_in_period>=freq.samples_per_period)
			{
				gen.low=0;
				gen.current_sample_in_period=gen.current_sample_in_period-freq.samples_per_period;
				gen.current_pulse_length=0;
			}

			if(!gen.low && gen.current_pulse_length<=gen.pulse_length)
			{
				val=1;
				gen.current_pulse_length++;
			}
			else
			{
				val=-1;
			}

			gen.current_sample_in_period++;
		}

		//========================================
		//finally put sample to audio output buffer
		buffer_out[i]=fmax //clamp lower
		(
			fmin //clamp upper
			(
				gen.dc_offset + ( val * gen.amplification )
				,gen.limit_high
			),
			gen.limit_low
		);
	}

	return 0;
}//end process()

//==================================================================

int main (int argc, char* argv[])
{
	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		printf ("could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_in = jack_port_register (client, "in", JACK_DEFAULT_OSC_TYPE, JackPortIsInput, 0);
	port_out = jack_port_register (client, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	if (port_in == NULL || port_out == NULL) 
	{
		fprintf (stderr, "could not register port\n");
		return 1;
	}
	else
	{
		printf ("registered JACK ports\n");
	}

#ifdef HAS_JACK_METADATA_API
	osc_port_uuid = jack_port_uuid(port_in);
	jack_set_property(client, osc_port_uuid, JACKEY_EVENT_TYPES, JACK_EVENT_TYPE__OSC, NULL);
#endif

	setup();
	print_all_properties();
	//minimal_test();

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	fprintf(stderr,"jack client name: %s\n",jack_get_client_name (client));

	if (jack_activate (client))
	{
		fprintf (stderr, "could not activate client\n");
		return 1;
	}

	printf("ready\n");

	/* run until interrupted */
	while (1) 
	{
		//sleep(1);
		usleep(100000);
	};

	jack_client_close(client);
	return 0;
}//end main()

//==================================================================

double herz_to_samples(double herz)
{
	return jack.sampling_rate/herz;
}
//[mm]
double herz_to_wavelength(double frequency)
{
	//wavelength = speed of sound / frequency
	double l=(double)freq.speed_of_sound/frequency;
	return (double)1000*l;


//frequency=speed of sound/wavelength

}
double samples_to_herz(double samples)
{
	return jack.sampling_rate/samples;
}
double bpm_to_samples(double bpm)
{
	return jack.sampling_rate/(bpm/60);
}
double samples_to_bpm(double samples)
{
	return 60*jack.sampling_rate/samples;
}
double beats_to_samples(double beats)
{
	return beats*bpm_to_samples(freq.beats_per_minute);
}
double samples_to_beats(double samples)
{
	return samples/beats_to_samples(1);
}
double nth_cycle_to_samples(double nth_cycle)
{
	if(nth_cycle==1)
	{
		return jack.samples_per_period;
	}
	return nth_cycle*jack.samples_per_period;
}
double samples_to_nth_cycle(double samples)
{
	if(samples==jack.samples_per_period)
	{
		return 1;
	}
	return samples/jack.samples_per_period;
}
//[mm]
double wavelength_to_samples(double wavelength)
{
	return herz_to_samples(wavelength_to_herz(wavelength));
}
double wavelength_to_herz(double wavelength) //[mm]
{
	//frequency = speed of sound / wavelength
	double f=(double)freq.speed_of_sound / (wavelength / 1000);
	return f;
}
double samples_to_wavelength(double samples)
{
	return herz_to_wavelength(samples_to_herz(samples));
}
double time_to_samples(double duration)
{
	return duration*jack.sampling_rate/1000;
}
//[ms]
double samples_to_time(double samples)
{
	return 1000*samples/jack.sampling_rate; 
}
double midi_note_to_samples(int midi_note_number)
{
	return herz_to_samples(midi_note_to_herz(midi_note_number));
}
double midi_note_symbol_to_samples(char* midi_note_symbol)
{
	//get compare to created symbols in array instead of parsing input
	int i;
	for(i=0;i<128;i++)
	{
		//case insensitive
		if(!strcasecmp(midi_notes[i].symbol,midi_note_symbol))
		{
			return midi_note_to_samples(i);
			break;
		}
	}

	fprintf(stderr,"warning: midi note symbol not found!\n");

	return freq.samples_per_period;
}
double midi_note_to_herz(int midi_note_number)
{
	//f = 2^((p-69)/12) * 440hz
	double f=pow(2,(double)(midi_note_number-69)/12) * freq.a4_ref;
	return f;
}
void set_pulse_length_auto()
{
	gen.pulse_length=(double)freq.samples_per_period/2;
}
void set_all_duration(double samples)
{
	if(samples<=0)
	{
		dur.infinite=1;
		dur.samples=0;
		dur.beats=0;
		dur.nth_cycle=0;
		dur.time=0;
	}
	else
	{
		dur.infinite=0;
		dur.samples=samples;
		dur.beats=samples_to_beats(samples);
		dur.nth_cycle=samples_to_nth_cycle(samples);
		dur.time=samples_to_time(samples);
	}

	//printf("%d %f %f %i %f\n",dur.infinite,dur.samples,dur.beats,dur.nth_cycle,dur.time);
}

void set_all_freq(double samples)
{
	freq.samples_per_period=samples;
	freq.herz=samples_to_herz(samples);
	freq.beats_per_minute=samples_to_bpm(samples);
	freq.nth_cycle=samples_to_nth_cycle(samples);
	freq.period_duration=samples_to_time(samples);
	freq.wavelength=samples_to_wavelength(samples);

	if(gen.pulse_length_auto==1)
	{
		set_pulse_length_auto();
	}
	else
	{
		if(gen.pulse_length>=freq.samples_per_period)
		{
			fprintf(stderr,"warning: pulse length is larger than or equally sized to shape period size!\n");
		}
	}

	gen.radstep=(double)(2*M_PI)/freq.samples_per_period;

/////////////////verbose
	print_all_properties();

}

void print_all_properties()
{
	fprintf(stderr,"---\n");
	fprintf(stderr,"jack sampling rate: %d\n",jack.sampling_rate);
	fprintf(stderr,"jack period size: %d\n",jack.samples_per_period);
	fprintf(stderr,"jack transport rolling: %d\n",jack.transport_state);

	fprintf(stderr,"samples per shape period: %f\n",freq.samples_per_period);
	fprintf(stderr,"herz: %f\n",freq.herz);
	fprintf(stderr,"bpm: %f\n",freq.beats_per_minute);
	fprintf(stderr,"nth cycle: %f\n",freq.nth_cycle);
	fprintf(stderr,"period duration [ms]: %f\n",freq.period_duration);

	fprintf(stderr,"a4 ref [hz]: %f\n",freq.a4_ref);

	int midi_index=find_nearest_midi_note(freq.samples_per_period);
	fprintf(stderr,"best match for MIDI note: %d (%s) (%f hz)\n"
		,midi_index,midi_notes[midi_index].symbol
		,samples_to_herz(midi_notes[midi_index].samples));

	fprintf(stderr,"speed of sound [m/s]: %f\n",freq.speed_of_sound);
	fprintf(stderr,"wavelength [mm]: %f\n",freq.wavelength);

	fprintf(stderr,"status: %d\n",gen.status);
	fprintf(stderr,"pulse length [samples]: %f\n",gen.pulse_length);

	fprintf(stderr,"amplification: %f\n",gen.amplification);
	fprintf(stderr,"dc_offset: %f\n",gen.dc_offset);

	fprintf(stderr,"auto pulse length on: %d\n",gen.pulse_length_auto);

	fprintf(stderr,"infinite duration: %d\n",dur.infinite);
	fprintf(stderr,"follow transport: %d\n",dur.follow_transport);
	fprintf(stderr,"duration samples: %f\n",dur.samples);
	fprintf(stderr,"duration beats: %f\n",dur.beats);
	fprintf(stderr,"duration nth_cycle: %f\n",dur.nth_cycle);
	fprintf(stderr,"duration time [ms]: %f\n",dur.time);

        fprintf(stderr,"radstep [rad]: %f\n",gen.radstep);

	//jack time, cycle frame number
}

int find_nearest_midi_note(float samples)
{
	float diff=0;
	float diff_prev=0;

	float diff_smallest=0;
	int index_smallest=0;

	int i;
        for(i=0;i<128;i++)
        {
		diff_prev=diff;
		diff=fabs(samples - midi_notes[i].samples);

		//fprintf(stderr,"diff prev: %f diff: %f\n",diff_prev,diff);

		if(diff<diff_prev)
		{
			diff_smallest=diff;
			index_smallest=i;
		}
	}
/*
	fprintf(stderr,"diff smallest %f index %i symbol %s samples %f freq hz %f\n"
		,diff_smallest,index_smallest,midi_notes[index_smallest].symbol
		,midi_notes[index_smallest].samples,samples_to_herz(midi_notes[index_smallest].samples));
*/

	return index_smallest;

}

void update_midi_notes()
{
	int i;
	for(i=0;i<128;i++)
	{
		midi_notes[i].samples=midi_note_to_samples(i);
	}
}

void create_midi_notes()
{
	//needs re-creation when a4 ref changes

	const char note_chars[7]="CDEFGAB";
	int note_index=0;
	int octave=-1;

	char* flat_sharp_char="#";

	int i;
	for(i=0;i<128;i++)
	{
		if(note_index>6)
		{
			note_index=0;
			octave++;
		}

		char _octave[16];
		sprintf(_octave, "%d", octave);

		strncpy(midi_notes[i].symbol,&note_chars[note_index],1);
		strcat(midi_notes[i].symbol,_octave);

		//implicit use of a4 ref
		midi_notes[i].samples=midi_note_to_samples(i);

/*
		printf("%d: %s %f %f\n",i,midi_notes[i].symbol,midi_note_to_herz(i),
			herz_to_wavelength(midi_note_to_herz(i)));
*/

		//add flat / sharp
		if
		(
			note_index==0 //C
			|| note_index==1 //D
			|| note_index==3 //F
			|| note_index==4 //G
			|| note_index==5 //A
		)
		{
			i++;
			if(i>127) {return;};

			strncpy(midi_notes[i].symbol,&note_chars[note_index],1);
			strncat(midi_notes[i].symbol,flat_sharp_char,1);
			strcat(midi_notes[i].symbol,_octave);

			//implicit use of a4 ref
			midi_notes[i].samples=midi_note_to_samples(i);

/*
			printf("%d: %s %f %f\n",i,midi_notes[i].symbol,midi_note_to_herz(i),
				herz_to_wavelength(midi_note_to_herz(i)));
*/
		}
		note_index++;
	}
}//end create_midi_notes()

//==================================================================

void clear_buffer(jack_nframes_t frames,jack_default_audio_sample_t* buff)
{
	int i;
	for(i=0;i<frames;i++)
	{
		buff[i]=0.0f;
	}
}

//==================================================================

static void signal_handler(int sig)
{
#ifdef HAS_JACK_METADATA_API
	jack_remove_property(client, osc_port_uuid, JACKEY_EVENT_TYPES);
#endif
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}
