/*
JACK client that connects to a serial device and receives bytes

rudimentary proof-of-concept to integrate serial devices directly in JACK ecosystem.

gcc -o jack_tty tty.c `pkg-config --libs liblo` `pkg-config --libs jack`

-the program tries to connect to the given serial device (/dev/ttyUSB0 @ 115200 by default)
-once connected, it tries to connect to the default JACK server
-if the device or JACK is disconnected, reconnection is tried until connected again
-hitting a key on the keyboard will send byte(s) to the serial device
-bytes/MIDI event buffers sent to jack_tty MIDI input are sent to device
-bytes read from the serial device are testwise interpreted as signed int (-128 to 127)
 to create a signal on the audio output port
-bytes read from the serial device are testwise put to JACK MIDI

-a serial device could send and read MIDI data
-received MIDI bytes need to be portioned in order to be forwarded to JACK MIDI (1,2,3 or n bytes per MIDI event)

-a serial device can also simply send and receive bytes with a custom meaning
 triggering anything (i.e. send MIDI, shape audio output, send to OSC event etc.) *inside* JACK

//tb/150903 tom@trellis.ch
*/

/*
//example code for a serial device (copy to Arduino IDE)
//-constantly sends bytes (0-255)
//-toggles LED for each received byte (nothing more done with received byte)
//(this program does not do anything meaningful and serves only as a test)

#define LED 13
bool on=true;
char val=0;
void setup()
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED,HIGH);  
  Serial.begin(115200);
}
void toggleLED(){on=!on;if(on){digitalWrite(LED,HIGH);}else{digitalWrite(LED,LOW);}}
void loop()
{
  if(Serial.available()>1)
  {
    toggleLED();
    char c=Serial.read();
  }
  Serial.print(val);
  if(val==255){val=0;}else{val++;}
  delay(1);
}
*/

/*
//example code for a serial MIDI device (copy to Arduino IDE)
//-receives note on and off messages on any channel
//-toggles LED according to note on/off
//-sends back several MIDI messages on note on/off
//(this program does not do anything meaningful and serves only as a test)
//see jack_midi_heartbeat as a note on/off deliverer

//https://github.com/FortySevenEffects/arduino_midi_library/
//http://arduinomidilib.fortyseveneffects.com/index.html
#include <MIDI.h>
//MIDI_CREATE_DEFAULT_INSTANCE();
//override default settings
struct MySettings : public midi::DefaultSettings
{
  static const long BaudRate = 115200; //31250; //since this is not a regular MIDI device, use max baudrate
  static const bool UseRunningStatus = false; //JACK MIDI is "normalized"
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI_, MySettings);
#define LED 13
void setup()
{
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  MIDI_.setHandleNoteOn(handleNoteOn); //attach handler
  MIDI_.setHandleNoteOff(handleNoteOff);
  MIDI_.begin(MIDI_CHANNEL_OMNI); //listen on all channels
  MIDI_.turnThruOff(); //don't echo back received MIDI events
}
void handleNoteOn(byte inChannel, byte inNote, byte inVelocity)
{
  digitalWrite(LED, HIGH); //turn on LED
  //sendRealTime (MidiType inType)

// !!! indicating msg size in bytes. this is a bit odd.
  Serial.write(0x01);
  MIDI_.sendRealTime(midi::Start); //start
  //sendPitchBend (int inPitchValue, Channel inChannel)
  Serial.write(0x03);
  MIDI_.sendPitchBend (1234, 8);
  Serial.write(0x03);
  MIDI_.sendNoteOn(42, 127, 1); // Send a Note (pitch 42, velo 127 on channel 1)
}
void handleNoteOff(byte inChannel, byte inNote, byte inVelocity)
{
  digitalWrite(LED, LOW); //turn off LED
  //MIDI.sendControlChange (DataByte inControlNumber, DataByte inControlValue, Channel inChannel)
  Serial.write(0x03);
  MIDI_.sendControlChange (25, 31, 1); //send back
  //sendProgramChange (DataByte inProgramNumber, Channel inChannel)
  Serial.write(0x02);
  MIDI_.sendProgramChange (16, 8);
  Serial.write(0x03);
  MIDI_.sendNoteOff(42, 0, 1); // Stop the note
}
void loop() //main loop
{
  MIDI_.read();
}
*/

/*
http://www.midi.org/techspecs/midimessages.php

https://ccrma.stanford.edu/~craig/articles/linuxmidi/misc/essenmidi.html
MIDI commands and data are distinguished according to the most significant bit of the byte. 
If there is a zero in the top bit, then the byte is a data byte, and if there is a one 
in the top bit, then the byte is a command byte. Here is how they are separated: 

    decimal     hexadecimal          binary
=======================================================
DATA bytes:
       0               0          00000000
     ...             ...               ...
     127              7F          01111111

COMMAND bytes:
     128              80          10000000
     ...             ...               ...
     255              FF          11111111

Furthermore, command bytes are split into half. The most significant half contains the 
actual MIDI command, and the second half contains the MIDI channel for which the command 
is for. For example, 0x91 is the note-on command for the second MIDI channel. the 9 
digit is the actual command for note-on and the digit 1 specifies the second channel 
(the first channel being 0). The 0xF0 set of commands do not follow this convention. 

   0x80     Note Off
   0x90     Note On
   0xA0     Aftertouch
   0xB0     Continuous controller
   0xC0     Patch change
   0xD0     Channel Pressure
   0xE0     Pitch bend
   0xF0     (non-musical commands)

The messages from 0x80 to 0xEF are called Channel Messages because the second four bits 
of the command specify which channel the message affects. 
The messages from 0xF0 to 0xFF are called System Messages; they do not affect any particular channel. 

A MIDI command plus its MIDI data parameters to be called a MIDI message. 

.----------------------------------------------------------------------------------------.
|The minimum size of a MIDI message is 1 byte (one command byte and no parameter bytes). |
|The maximum size of a MIDI message (not considering 0xF0 commands) is three bytes .     |
-----------------------------------------------------------------------------------------.

A MIDI message always starts with a command byte. Here is a table of the MIDI messages 
that are possible in the MIDI protocol: 

Command Meaning 		# parameters 	param 1 	param 2
0x80 	Note-off 		2 		key 		velocity
0x90 	Note-on 		2 		key 		veolcity
0xA0 	Aftertouch 		2 		key 		touch
0xB0 	Continuous controller 	2 		controller # 	controller value
0xC0 	Program change 		1 		instrument # 			(! only one param)
0xD0 	Channel Pressure 	1 		pressure
0xE0 	Pitch bend 		2 		lsb (7 bits) 	msb (7 bits)	(!!!)
0xF0 	(non-musical commands) 			

Command Meaning 				# param
0xF0 	start of system exclusive message 	variable
0xF1 	MIDI Time Code Quarter Frame (Sys Common)
0xF2 	Song Position Pointer (Sys Common)
0xF3 	Song Select (Sys Common)
0xF4 	???
0xF5 	???
0xF6 	Tune Request (Sys Common)
0xF7 	end of system exclusive message 	0
0xF8 	Timing Clock (Sys Realtime) 
0xFA 	Start (Sys Realtime)
0xFB 	Continue (Sys Realtime)
0xFC 	Stop (Sys Realtime)	
0xFD 	???
0xFE 	Active Sensing (Sys Realtime)
0xFF 	System Reset (Sys Realtime)	
*/


//tty.c uses (altered) parts of com.c, see below
//to retrieve original com.c from this file:
//cat tty.c | grep -A100 "/*__com.c" | grep -v "^/\*__com.c$" | grep -v "^*/$" | base64 -d | gunzip -
/* 
  com.c
  Homepage: http://tinyserial.sourceforge.net
  Version : 2009-03-05
 
  Ivan Tikhonov, http://www.brokestream.com, kefeer@brokestream.com
  Patches by Jim Kou, Henry Nestler, Jon Miner, Alan Horstmann

  Copyright (C) 2007 Ivan Tikhonov

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Ivan Tikhonov, kefeer@brokestream.com
*/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include <termios.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>

int main(int argc, char *argv[]);
static void setup_serial_thread();
static void start_serial_thread();
static void *serial_thread_func(void *arg);
static void signal_handler(int sig);
static void shutdown_callback(void *arg);
static void jack_error(const char* err);
static int process(jack_nframes_t nframes, void *arg);
static int transfer_byte(int from, int to, int is_control);
static void print_status(int fd);

static int comfd;
struct termios oldtio, newtio; //place for old and new port settings for serial por
struct termios oldkey, newkey; //place tor old and new port settings for keyboard t

typedef struct {char *name; int flag; } speed_spec;

static char *devicename = "/dev/ttyUSB0";
//char *devicename = "/dev/ttyACM0";

static jack_ringbuffer_t *rb=NULL;

speed_spec speeds[] =
{
	{"1200", B1200},
	{"2400", B2400},
	{"4800", B4800},
	{"9600", B9600},
	{"19200", B19200},
	{"38400", B38400},
	{"57600", B57600},
	{"115200", B115200},
	{NULL, 0}
};
int speed = B115200;
int speed_int=115200;

typedef jack_default_audio_sample_t sample_t;

static jack_client_t *client;

static const char **ports;
static jack_port_t **ioPortArray;

static jack_port_t* port_in_midi;
static jack_port_t* port_out_midi;

void* buffer_in_midi;
void* buffer_out_midi;

static int output_port_count=1;

static int process_enabled=0;
static int connection_to_jack_down=1;

//serial thread
static pthread_t serial_thread={0};
static pthread_mutex_t serial_thread_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ok_to_read=PTHREAD_COND_INITIALIZER;
static int serial_thread_initialized=0;

//serial byte value
static int current_value=0;
static float output_value=0;

//===================================================================
int main(int argc, char *argv[])
{
	// Make output unbuffered
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if(argc >= 2 &&
		(strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
	{
		printf("connect JACK client to serial device\n\n");
		printf("syntax: jack_tty <serial device> <speed>\n\n");
		printf("default values: /dev/ttyUSB0 115200\n");
		printf("example: jack_tty /dev/ttyACM0 9600\n");

		printf("jack_tty source at https://github.com/7890/jack_tools\n\n");
		return(0);
/*
		printf("  /tty/status\n");
		printf("  /tty/open si \"/dev/ttyUSB1\" 115200\n");
		printf("  /tty/close\n");

		printf("  /tty/in c 'x'\n");
		printf("  /tty/in s \"hello x\"\n");
		printf("  /tty/in b (blob)\n\n");

		printf("  /tty/status isi 1 \"/dev/ttyUSB1\" 115200\n"); //connected, device, speed

		printf("  /tty/out c 'x'\n");
		printf("  /tty/out s \"hello x\"\n");
		printf("  /tty/out b (blob)\n");
*/
	}

	//speed
	if(argc >= 3)
	{
		int i=atoi(argv[2]);
		speed_int=i;
		switch(i)
		{
			case 1200: speed=B1200; break;
			case 2400: speed=B2400; break;
			case 4800: speed=B4800; break;
			case 9600: speed=B9600; break;
			case 19200: speed=B19200; break;
			case 38400: speed=B38400; break;
			case 57600: speed=57600; break;
			case 115200: speed=B115200; break;
			default: speed=B115200; speed_int=115200; fprintf(stderr,"invalid speed, using default %d\n",speed_int); break;
		}
	}

	//serial device
	if(argc >= 2)
	{
		devicename=argv[1];
	}
 
	setup_serial_thread();
	while(!serial_thread_initialized)
	{
		usleep(500);
	}

	rb=jack_ringbuffer_create(100000);

	//jack_options_t options=JackNullOption;
	jack_options_t options=JackNoStartServer;
	jack_status_t status;

	ioPortArray = (jack_port_t**) calloc(
		output_port_count * sizeof(jack_port_t*), sizeof(jack_port_t*));

	jack_set_error_function(jack_error);

	//outer loop, wait and reconnect to jack
	while(1==1)
	{
	connection_to_jack_down=1;
	fprintf(stderr,"\r\n");
	fprintf(stderr,"waiting for connection to JACK...\n\r");
	while(connection_to_jack_down)
	{
		if((client=jack_client_open("tty", options, &status, NULL))==0)
		{
//			fprintf(stderr, "jack_client_open() failed, ""status=0x%2.0x\n", status);
			if(status & JackServerFailed)
			{
//				fprintf(stderr, "Unable to connect to JACK server\n");
			}
			usleep(1000000);
		}
		else
		{
			connection_to_jack_down=0;
			fprintf(stderr,"connected to JACK.\n\r");
		}
	}

	jack_on_shutdown(client, shutdown_callback, NULL);

	jack_set_process_callback (client, process, NULL);

	port_in_midi = jack_port_register (client, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	port_out_midi = jack_port_register (client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	//register each audio output port
	int port_=0;
	for (port_=0 ; port_<output_port_count ; port_ ++)
	{
		//create port name
		char* portName;
		if (asprintf(&portName, "output_%d", (port_+1)) < 0) 
		{
			fprintf(stderr, "/!\\ a could not create portname for port %d\n", port_);
			exit(1);
		}

		//register the output port
		ioPortArray[port_] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		if (ioPortArray[port_] == NULL) 
		{
			fprintf(stderr, "/!\\ b could not create output port %d\n", (port_+1));
			exit(1);
		}
	}

	if(jack_activate(client))
	{
		fprintf(stderr, "cannot activate client");
		exit(1);
	}

	start_serial_thread();

	process_enabled=1;

	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGTERM, signal_handler);

	while(1==1)
	{
		if(connection_to_jack_down)
		{
			goto _continue;
		}
		usleep(10000);
	}

_continue:

	usleep(10000);
}//end while true outer loop

	exit(0);
}//end main

//=============================================================================
static void setup_serial_thread()
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//initially lock
	pthread_mutex_lock(&serial_thread_lock);
	//create the serial_thread (pthread_t) with start routine serial_thread_func
	//serial_thread_func will be called after thread creation
	//(no attributes, no args)
	pthread_create(&serial_thread, NULL, serial_thread_func, NULL);
}

//=============================================================================
static void start_serial_thread()
{
	if(pthread_mutex_trylock (&serial_thread_lock)==0)
	{
		//signal to serial_thread it can start or continue to read
		pthread_cond_signal (&ok_to_read);
		//unlock again
		pthread_mutex_unlock (&serial_thread_lock);
	}
	else
	{
//		fprintf(stderr,"/!\\ could not lock mutex.\n");
	}
}//end start_serial_thread()

//this method is called from serial_thread (pthread_t)
//it will be called only once and then loop/wait until a condition to finish is met
//=============================================================================
static void *serial_thread_func(void *arg)
{
	int need_exit = 0;

_init:

	fprintf(stderr,"CTRL+c to quit\n");

//	fprintf(stderr,"serial_thread_func() start\n");
	fprintf(stderr,"waiting for serial device '%s' @ %d baud...\n",devicename,speed_int);

	//(re)try open serial device
	while(1)
	{
		comfd = open(devicename, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if (comfd < 0)
		{
			//perror(devicename);
			close(comfd);
			usleep(1000000);
		}
		else
		{
			break;
		}
	}

	fprintf(stderr,"connected to serial device.\n");
	fprintf(stderr,"CTRL+a+c to quit\n");

	tcgetattr(STDIN_FILENO,&oldkey);
	newkey.c_cflag = B9600 | CRTSCTS | CS8 | CLOCAL | CREAD;
	newkey.c_iflag = IGNPAR;
	newkey.c_oflag = 0;
	newkey.c_lflag = 0;
	newkey.c_cc[VMIN]=1;
	newkey.c_cc[VTIME]=0;
	tcflush(STDIN_FILENO, TCIFLUSH);
	tcsetattr(STDIN_FILENO,TCSANOW,&newkey);

	tcgetattr(comfd,&oldtio); // save current port settings 
	newtio.c_cflag = speed | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN]=1;
	newtio.c_cc[VTIME]=0;
	tcflush(comfd, TCIFLUSH);
	tcsetattr(comfd,TCSANOW,&newtio);

	print_status(comfd);

	//only wait the first time
	if(!serial_thread_initialized)
	{
		//all setup for start_serial_thread
		serial_thread_initialized=1;

		//===wait here until started
		pthread_cond_wait (&ok_to_read, &serial_thread_lock);
	}

	while(!need_exit)
	{
		fd_set fds;
		int ret;
		
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(comfd, &fds);

		ret = select(comfd+1, &fds, NULL, NULL, NULL);
		if(ret == -1)
		{
			perror("select");
		}
		else if (ret > 0)
		{
			if(FD_ISSET(STDIN_FILENO, &fds)) 
			{
//				fprintf(stderr,"SERIAL< ");//going to serial
				need_exit = transfer_byte(STDIN_FILENO, comfd, 1);
			}

			if(FD_ISSET(comfd, &fds))
			{
//				fprintf(stderr,"SERIAL> "); //coming from serial
//				need_exit = transfer_byte(comfd, STDIN_FILENO, 0);

				char c;
				int ret;
				do
				{
					ret = read(comfd, &c, 1);
					jack_ringbuffer_write(rb,&c,1);
					current_value=(int)c;
//					fprintf(stderr,"!%d ",c);
				} while (ret < 0 && errno == EINTR);
				if(ret != 1)
				{
					fprintf(stderr, "\n\rnothing to read. probably serial device disconnected.\n\r");
					need_exit=-2;
				}
			}
		}
	}//end while(!need_exit)

	close(comfd);
	tcsetattr(comfd,TCSANOW,&oldtio);
	tcsetattr(STDIN_FILENO,TCSANOW,&oldkey);

	usleep(1000000);
	need_exit=0;
	goto _init;

_done:
	tcsetattr(comfd,TCSANOW,&oldtio);
	tcsetattr(STDIN_FILENO,TCSANOW,&oldkey);

	pthread_mutex_unlock (&serial_thread_lock);

	return 0;
}//end serial_thread_func()

//===================================================================
static void signal_handler(int sig)
{
	fprintf(stderr, "signal received, exiting ...\n");

	if(!connection_to_jack_down)
	{
		jack_client_close(client);
	}
	exit(0);
}

//================================================================
static void shutdown_callback(void *arg)
{
	connection_to_jack_down=1;
	fprintf(stderr, "JACK server down!\n");
}

//===================================================================
static void jack_error(const char* err)
{
	//suppress for now
}

//=============================================================================
static int process(jack_nframes_t nframes, void *arg)
{
	if(!process_enabled)
	{
		return 0;
	}

	//prepare receive buffer
	buffer_in_midi = jack_port_get_buffer (port_in_midi, nframes);
	buffer_out_midi = jack_port_get_buffer (port_out_midi, nframes);
	jack_midi_clear_buffer(buffer_out_midi);

	//process incoming messages from JACK
	int msgCount = jack_midi_get_event_count (buffer_in_midi);

	int i;
	//iterate over encapsulated osc messages
	for (i = 0; i < msgCount; ++i) 
	{
		jack_midi_event_t event;
		int r;

		r = jack_midi_event_get (&event, buffer_in_midi, i);
		if (r == 0)
		{
			//check if midi data, skip if other
			if(*event.buffer=='/')
			{
				fprintf(stderr,"OSC> #%d len %lu\n\r",msgCount,event.size);
				//do stuff here
			}
			else
			{
//				fprintf(stderr,"MIDI> #%d len %lu\n\r",msgCount,event.size);//,event.buffer);
				//write to serial
				int x=write(comfd,(void*)event.buffer,event.size);
			}
		}
	}

	//put MIDI bytes from serial to JACK
/*
	//old approach. only good for 3 byte messages (any other count will disturb the flow)
	while(jack_ringbuffer_read_space(rb)>=3)
	{
		void *buf;
		buf=malloc(3);
		jack_ringbuffer_read(rb,buf,3);
		jack_midi_event_write(buffer_out_midi,pos,(const jack_midi_data_t *)buf,3);
	}
*/

	//parse bytestream here to single MIDI messages (one / two / n bytes packages)

	//something is still fishy

	int pos=0; //!!!! timing wrong, all messages at start of period
	int msg_len=0;

	while(jack_ringbuffer_read_space(rb)>0)
	{
		char c;
		//read one byte
		jack_ringbuffer_peek(rb,&c,1);
		msg_len=(int)c;

		if(msg_len>3)
		{
			//this is not a size byte. skip it
			jack_ringbuffer_read_advance(rb,1);
			continue;
		}

//		fprintf(stderr,"\n\rmsg length %d ",msg_len);

		if(jack_ringbuffer_read_space(rb)>msg_len+1)
		{
			//skip size byte
			jack_ringbuffer_read_advance(rb,1);
			void *buf;
			buf=malloc(msg_len);
			//read it
			jack_ringbuffer_read(rb,buf,msg_len);

//			fprintf(stderr,"= %d %s",msg_len,(char*)buf);

			//put to JACK MIDI out
			jack_midi_event_write(buffer_out_midi,pos,(const jack_midi_data_t *)buf,msg_len);
		}
		else
		{
			break;
		}
		pos++; //assume less midi events than period has samples
	}

	//fill audio buffers
	//set value using last received serial byte
	//(this audio stream will need smoothing depending on the usecase)
	for(i=0; i<output_port_count; i++)
	{
		sample_t *o1;
		//get output buffer from JACK for that channel
		o1=(sample_t*)jack_port_get_buffer(ioPortArray[i],nframes);

		//set all samples zero
		//memset(o1, 0, nframes*4);

		int k=0;
		for(k=0; k<nframes;k++)
		{
			output_value=(float)current_value/127;
			//fprintf(stderr,"\n\r%f\n\r",output_value);

			o1[k]=output_value;
		}
	}

	return 0;
}

//=============================================================================
static void print_status(int fd)
{
	int status;
	unsigned int arg;
	status = ioctl(fd, TIOCMGET, &arg);
	fprintf(stderr, "[STATUS]: ");
	if(arg & TIOCM_RTS) fprintf(stderr, "RTS ");
	if(arg & TIOCM_CTS) fprintf(stderr, "CTS ");
	if(arg & TIOCM_DSR) fprintf(stderr, "DSR ");
	if(arg & TIOCM_CAR) fprintf(stderr, "DCD ");
	if(arg & TIOCM_DTR) fprintf(stderr, "DTR ");
	if(arg & TIOCM_RNG) fprintf(stderr, "RI ");
	fprintf(stderr, "\r\n");
}

//=============================================================================
static int transfer_byte(int from, int to, int is_control)
{
	char c;
	int ret;
	do
	{
		ret = read(from, &c, 1);
	} while (ret < 0 && errno == EINTR);
	if(ret == 1)
	{
		if(is_control)
		{
			if(c == '\x01')  // C-a
			{
				return -1;
			} else if(c == '\x18')  // C-x
			{
				print_status(to);
				return 0;
			}
		}
/*
		else
		{
			current_value=(int)c;
			//fprintf(stderr,"\n\r%d\n\r",(int)current_value);
		}
*/
		while(write(to, &c, 1) == -1) {
			if(errno!=EAGAIN && errno!=EINTR) { perror("write failed"); break; }
		}
	}
	else
	{
		fprintf(stderr, "\n\rnothing to read. probably serial device disconnected.\n\r");
		return -2;
	}

	return 0;
}//end transfer_byte

/*__com.c
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
JtRcuJESAAA=
*/
