// ----------------------------------------------------------------------------
//
//  Copyright (C) 2015 Thomas Brand <tom@trellis.ch>
//    
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//  This file is part of jack_playfile
//  https://github.com/7890/jack_tools/
//
//tb/150612+
// ----------------------------------------------------------------------------

#ifndef CONTROL_H_INC
#define CONTROL_H_INC

#include <sys/stat.h>
#include <fcntl.h>

#ifndef WIN32
	#include <termios.h>
#endif

#include "config.h"
#include "jack_playfile.h"

#ifndef WIN32
	static struct termios initial_settings; //cooked
	struct termios settings; //raw

	//lower values mean faster repetition of events from held key (~ ?)
	#define MAGIC_MAX_CHARS 5//18
	static unsigned char keycodes[ MAGIC_MAX_CHARS ];
#else
	DWORD        w_term_mode;
	HANDLE       w_term_hstdin;
	INPUT_RECORD w_term_inrec;
	DWORD        w_term_count;
#endif

static int KEY_SPACE=0;
static int KEY_Q=0;
static int KEY_H=0;
static int KEY_F1=0;
static int KEY_ARROW_LEFT=0;
static int KEY_ARROW_RIGHT=0;
static int KEY_ARROW_UP=0;
static int KEY_ARROW_DOWN=0;
static int KEY_HOME=0;
static int KEY_END=0;
static int KEY_BACKSPACE=0;
static int KEY_M=0;
static int KEY_L=0;
static int KEY_COMMA=0;
static int KEY_PERIOD=0;
static int KEY_DASH=0;
static int KEY_C=0;
static int KEY_P=0;
static int KEY_ENTER=0;
static int KEY_0=0;

static const char *clear_to_eol_seq=NULL;
static const char *turn_on_cursor_seq=NULL;
static const char *turn_off_cursor_seq=NULL;

//for displaying 'wheel' as progress indicator
static int wheel_state=0;

static void print_keyboard_shortcuts();
static void handle_key_hits();
static void print_clock();
static void print_next_wheel_state(int direction);
static void set_terminal_raw();
static int read_raw_key();
static void reset_terminal();
static void init_term_seq();
static void init_key_codes();

//=============================================================================
static void print_keyboard_shortcuts()
{
	fprintf(stderr,"\r%s\r\n",clear_to_eol_seq);
	fprintf(stderr,"HELP\n\n");

//      fprintf(stderr,"\r%s\rkeyboard shortcuts:\n",clear_to_eol_seq);
	fprintf(stderr,"keyboard shortcuts:\n\n");

	fprintf(stderr,"  h, f1:             help (this screen)\n");
	fprintf(stderr,"  space:             toggle play/pause\n");
	fprintf(stderr,"  enter:             play\n");
	fprintf(stderr,"  < arrow left:      seek one step backward\n");
	fprintf(stderr,"  > arrow right:     seek one step forward\n");
	fprintf(stderr,"  ^ arrow up:        increment seek step size\n");
	fprintf(stderr,"  v arrow down:      decrement seek step size\n");
	fprintf(stderr,"  home               seek to start\n");
	fprintf(stderr,"  0:                 seek to start and pause\n");
	fprintf(stderr,"  backspace:         seek to start and play\n");
	fprintf(stderr,"  end:               seek to end\n");
	fprintf(stderr,"  m:                 toggle mute on/off*\n");
	fprintf(stderr,"  l:                 toggle loop on/off*\n");
	fprintf(stderr,"  p:                 toggle pause at end on/off*\n");
	fprintf(stderr,"  c:                 toggle clock display on*/off\n");
	fprintf(stderr,"  , comma:           toggle clock seconds* /frames\n");
	fprintf(stderr,"  . period:          toggle clock absolute*/relative\n");
	fprintf(stderr,"  - dash:            toggle clock elapsed* /remaining\n");
	fprintf(stderr,"  q:                 quit\n\n");

	fprintf(stderr,"prompt:\n\n");
	fprintf(stderr,"|| paused   MLP  S rel 0.001       943.1  (00:15:43.070)   \n");
	fprintf(stderr,"^           ^^^  ^ ^   ^     ^     ^     ^ ^             ^ \n");
	fprintf(stderr,"1           234  5 6   7     8     9     8 10            11\n\n");
	fprintf(stderr,"  1): status playing '>', paused '||' or seeking '...'\n");
	fprintf(stderr,"  2): mute on/off 'M' or ' '\n");
	fprintf(stderr,"  3): loop on/off 'L' or ' '\n");
	fprintf(stderr,"  4): pause at end on/off 'P' or ' '\n");
	fprintf(stderr,"  5): time and seek in seconds 'S' or frames 'F'\n");
	fprintf(stderr,"  6): time indication 'rel' to frame_offset or 'abs'\n");
	fprintf(stderr,"  7): seek step size in seconds or frames\n");
	fprintf(stderr,"  8): time elapsed ' ' or remaining '-'\n");
	fprintf(stderr,"  9): time in seconds or frames\n");
	fprintf(stderr," 10): time in HMS.millis\n");
	fprintf(stderr," 11): keyboard input indication (i.e. seek)\n\n");

	//need command to print current props (file, offset etc)
}//end print_keyboard_shortcuts()

//=============================================================================
static void handle_key_hits()
{
//the single cases could be further put to actions / separate methods
//since some status updates are made.
//should call method for each command and decide in the method what to do


//>  playing   ML  1234.5 (0:12:34.1)  << (``)  
//^            ^^  ^       ^           ^  ^
	int rawkey=read_raw_key();
//	fprintf(stderr,"rawkey: %d\n",rawkey);

	//no key while timeout not reached
	if(rawkey==0)
	{
		//clear
//      	fprintf(stderr,"%s",clear_to_eol_seq);
	}

	//'space': toggle play/pause
	else if(rawkey==KEY_SPACE)
	{
		if(pause_at_end && is_idling_at_end)
		{
			fprintf(stderr,"idle at end");
		}
		else
		{
			is_idling_at_end=0;
			is_playing=!is_playing;
		}
	}
	//'enter': play
	else if(rawkey==KEY_ENTER)
	{
		if(pause_at_end && is_idling_at_end)
		{
			fprintf(stderr,"idle at end");
		}
		else
		{
			is_idling_at_end=0;
			is_playing=1;
		}
	}
	//'q': quit
	else if(rawkey==KEY_Q)
	{
		fprintf(stderr,"\r%s\rquit received\n",clear_to_eol_seq);
		shutdown_in_progress=1;
	}
	//'h' or 'f1': help
	else if(rawkey==KEY_H || rawkey==KEY_F1)
	{
		print_keyboard_shortcuts();
	}
	//'<' (arrow left): 
	else if(rawkey==KEY_ARROW_LEFT)
	{
		fprintf(stderr,"<< ");
		print_next_wheel_state(-1);
		seek_frames(-seek_frames_per_hit);
	}
	//'>' (arrow right): 
	else if(rawkey==KEY_ARROW_RIGHT)
	{
		if(pause_at_end && is_idling_at_end)
		{
			fprintf(stderr,"idle at end");
		}
		else
		{
			is_idling_at_end=0;
			fprintf(stderr,">> ");
			print_next_wheel_state(+1);
			seek_frames( seek_frames_per_hit);
		}
	}
	//'^' (arrow up):
	else if(rawkey==KEY_ARROW_UP)
	{
		fprintf(stderr,"^^ inc step");
		increment_seek_step_size();
	}
	//'v' (arrow down):
	else if(rawkey==KEY_ARROW_DOWN)
	{
		fprintf(stderr,"vv dec step");
		decrement_seek_step_size();
	}
	//'|<' (home):
	else if(rawkey==KEY_HOME)
	{
		fprintf(stderr,"|< home ");
		seek_frames_absolute(frame_offset);
	}
	//'|< && >' (backspace):
	else if(rawkey==KEY_BACKSPACE)
	{
		fprintf(stderr,"|< home play");
		is_playing=0;
		seek_frames_absolute(frame_offset);
		is_playing=1;
	}
	//'|< && ||' (0):
	else if(rawkey==KEY_0)
	{
		fprintf(stderr,"|< home pause");
		is_playing=0;
		seek_frames_absolute(frame_offset);
	}
	//'>|' (end):
	else if(rawkey==KEY_END)
	{
		if(is_idling_at_end)
		{
				fprintf(stderr,"idle at end");
		}
		else
		{
			fprintf(stderr,">| end ");
			seek_frames_absolute(frame_offset+frame_count);
		}
	}
	//'m': toggle mute
	else if(rawkey==KEY_M)
	{
		is_muted=!is_muted;
		fprintf(stderr,"mute %s",is_muted ? "on " : "off ");
	}
	//'l': loop
	else if(rawkey==KEY_L)
	{
		loop_enabled=!loop_enabled;

		if(loop_enabled && all_frames_read)
		{
			seek_frames_absolute(frame_offset);
		}

		fprintf(stderr,"loop %s",loop_enabled ? "on " : "off ");
	}
	//'p': pause at end
	else if(rawkey==KEY_P)
	{
		pause_at_end=!pause_at_end;
		if(pause_at_end && all_frames_read)
		{
			is_idling_at_end=1;
		}

		fprintf(stderr,"pae %s",pause_at_end ? "on " : "off ");
	}
	//',':  toggle seconds/frames
	else if(rawkey==KEY_COMMA)
	{
		is_time_seconds=!is_time_seconds;
		fprintf(stderr,"time %s",is_time_seconds ? "seconds " : "frames ");

		if(is_time_seconds)
		{
			set_seconds_from_exponent();;
		}
		else
		{
			set_frames_from_exponent();
		}
	}
	//'-': toggle elapsed/remaining
	else if(rawkey==KEY_DASH)
	{
		is_time_elapsed=!is_time_elapsed;
		fprintf(stderr,"time %s",is_time_elapsed ? "elapsed " : "remaining ");
	}
	//'.': toggle abs / rel
	else if(rawkey==KEY_PERIOD)
	{
		is_time_absolute=!is_time_absolute;
		fprintf(stderr,"time %s",is_time_absolute ? "abs " : "rel ");
	}
	//'c': toggle clock on/off
	else if(rawkey==KEY_C)
	{
		is_clock_displayed=!is_clock_displayed;
		fprintf(stderr,"clock %s", is_clock_displayed ? "on " : "off ");
	}

#ifndef WIN32
		fprintf(stderr,"%s",clear_to_eol_seq);
#else
		fprintf(stderr,"                 ");
#endif

}//end handle_key_hits()

//=============================================================================
static void print_clock()
{
	if(!is_clock_displayed)
	{
		return;
	}

	sf_count_t pos=sin_seek(0,SEEK_CUR);
	double seconds=0;

/*

 elapsed                     remaining
|--------------------------|------------------|  abs

               elapsed       rem
             |-------------|-----|
                           v
|------------|-------------------|------------|  rel
             off                 off+count
*/

	if(!is_time_absolute)
	{
		pos-=frame_offset;

		if(!is_time_elapsed)
		{
			pos=frame_count-pos;
		}
	}
	else //absolute
	{
		if(!is_time_elapsed)
		{
			pos=sf_info_generic.frames-pos;
		}
	}

	seconds=frames_to_seconds(pos,sf_info_generic.samplerate);

	if(is_time_seconds)
	{
		if(seek_seconds_per_hit<1)
		{
			fprintf(stderr,"S %s %.3f %s %9.1f %s(%s) "
				,(is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(is_time_elapsed ? " " : "-")
				,frames_to_seconds(pos,sf_info_generic.samplerate)
				,(is_time_elapsed ? " " : "-")
				,format_duration_str(seconds));
		}
		else
		{
			fprintf(stderr,"S %s %5.0f  %s %9.1f %s(%s) "
				,(is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(is_time_elapsed ? " " : "-")
				,frames_to_seconds(pos,sf_info_generic.samplerate)
				,(is_time_elapsed ? " " : "-")
				,format_duration_str(seconds));
		}
	}
	else
	{
		//indicate high frame seek numbers witn k(ilo) and M(ega)
		uint64_t seek_fph=0;
		char *scale=" ";
		if(seek_frames_per_hit>1000000)
		{
			seek_fph=seek_frames_per_hit/1000000;
			scale="M";
		}
		else if(seek_frames_per_hit>1000)
		{
			seek_fph=seek_frames_per_hit/1000;
			scale="k";
		}
		else
		{
			seek_fph=seek_frames_per_hit;
		}

		fprintf(stderr,"F %s %5"PRId64"%s %s %9"PRId64" %s(%s) "
			,(is_time_absolute ? "abs" : "rel")
			,seek_fph
			,scale
			,(is_time_elapsed ? " " : "-")
			,pos
			,(is_time_elapsed ? " " : "-")
			,format_duration_str(seconds));
	}
}// end print_clock()

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
}//end print_next_wheel_state()

//=============================================================================
//http://www.cplusplus.com/forum/articles/7312/#msg33734
static void set_terminal_raw()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//save original tty settings ("cooked")
	tcgetattr( STDIN_FILENO, &initial_settings );

	tcgetattr( STDIN_FILENO, &settings );

	//set the console mode to no-echo, raw input
	settings.c_cc[ VTIME ] = 1;
	settings.c_cc[ VMIN  ] = MAGIC_MAX_CHARS;
	settings.c_iflag &= ~(IXOFF);
	settings.c_lflag &= ~(ECHO | ICANON);
	tcsetattr( STDIN_FILENO, TCSANOW, &settings );

	//turn off cursor
	fprintf(stderr,"%s",turn_off_cursor_seq);//

	//in shutdown signal handler
	//tcsetattr( STDIN_FILENO, TCSANOW, &initial_settings );
#else
	//set the console mode to no-echo, raw input, and no window or mouse events
	w_term_hstdin = GetStdHandle( STD_INPUT_HANDLE );
	if (w_term_hstdin == INVALID_HANDLE_VALUE
		|| !GetConsoleMode( w_term_hstdin, &w_term_mode )
		|| !SetConsoleMode( w_term_hstdin, 0 ))
	{
		fprintf(stderr,"/!\\ could not initialize terminal\n");
		return;
	}
	FlushConsoleInputBuffer( w_term_hstdin );

	//turn off cursor
	//...
#endif
}//end set_terminal_raw()

//=============================================================================
static int read_raw_key()
{
#ifndef WIN32
	//non-blocking poll / read key
	//http://stackoverflow.com/questions/3711830/set-a-timeout-for-reading-stdin
	fd_set selectset;
	struct timeval timeout = {0,100000}; //timeout seconds, microseconds
	int ret;
	FD_ZERO(&selectset);
	FD_SET(0,&selectset);
	ret =  select(1,&selectset,NULL,NULL,&timeout);
	if(ret == 0)
	{
		//timeout
		return 0;
	}
	else if(ret == -1)
	{
		//error
		return 0;
	}
	//else if ret>0
	//stdin has data, read it
	//(we know stdin is readable, since we only asked for read events
	//and stdin is the only fd in our select set.

	int count = read( STDIN_FILENO, (void*)keycodes, MAGIC_MAX_CHARS );

	return (count == 1)
		? keycodes[ 0 ]
		: -(int)(keycodes[ count -1 ]);
#else
	//https://msdn.microsoft.com/en-us/library/windows/desktop/ms687032%28v=vs.85%29.aspx
	//get a single key PRESS

	if (WaitForSingleObject(w_term_hstdin, 100) != WAIT_OBJECT_0)
	{
		return 0;
	}
/*
	do ReadConsoleInput( w_term_hstdin, &w_term_inrec, 1, &w_term_count );
	while ((w_term_inrec.EventType != KEY_EVENT) || !w_term_inrec.Event.KeyEvent.bKeyDown);
*/

	ReadConsoleInput( w_term_hstdin, &w_term_inrec, 1, &w_term_count );
	if((w_term_inrec.EventType != KEY_EVENT) || !w_term_inrec.Event.KeyEvent.bKeyDown)
	{
		return 0;
	}

	//restore the console to its previous state
	SetConsoleMode( w_term_hstdin, w_term_mode );

	return w_term_inrec.Event.KeyEvent.wVirtualKeyCode;
#endif
}//end read_raw_key()

//=============================================================================
static void reset_terminal()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//reset terminal to original settings
	tcsetattr( STDIN_FILENO, TCSANOW, &initial_settings );

	//turn on cursor
	fprintf(stderr,"%s",turn_on_cursor_seq);
#endif
}

//=============================================================================
static void init_term_seq()
{
#ifndef WIN32
	clear_to_eol_seq=       "\033[0J";
	turn_off_cursor_seq=    "\033[?25l";
	turn_on_cursor_seq=     "\033[?25h";
#else
	//                       ---------------------------------------------------------------------
	clear_to_eol_seq=       "                                                                     ";
	turn_on_cursor_seq=     "";
	turn_on_cursor_seq=     "";
#endif
}

//=============================================================================
static void init_key_codes()
{
	if(!keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	KEY_SPACE=32;
	KEY_Q=113;
	KEY_H=104;
	KEY_F1=-80;
	KEY_ARROW_LEFT=-68;
	KEY_ARROW_RIGHT=-67;
	KEY_ARROW_UP=-65;
	KEY_ARROW_DOWN=-66;
	KEY_HOME=-72;
	KEY_END=-70;
	KEY_BACKSPACE=127;
	KEY_M=109;
	KEY_L=108;
	KEY_COMMA=44;
	KEY_PERIOD=46;
	KEY_DASH=45;
	KEY_C=99;
	KEY_P=112;
	KEY_ENTER=10;
	KEY_0=48;
#else
	KEY_SPACE=32;
	KEY_Q=81;
	KEY_H=72;
	KEY_F1=112;
	KEY_ARROW_LEFT=37;
	KEY_ARROW_RIGHT=39;
	KEY_ARROW_UP=38;
	KEY_ARROW_DOWN=40;
	KEY_HOME=36;
	KEY_END=35;
	KEY_BACKSPACE=8;
	KEY_M=77;
	KEY_L=76;
	KEY_COMMA=188;
	KEY_PERIOD=190;
	KEY_DASH=189;
	KEY_C=67;///////
	KEY_P=80;///
	KEY_ENTER=10;////////
	KEY_0=48;/////
#endif
}//init_key_codes()

#endif
//EOF
