// ----------------------------------------------------------------------------
//
//  Copyright (C) 2015 - 2016 Thomas Brand <tom@trellis.ch>
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

#ifndef KB_CONTROL_H_INC
#define KB_CONTROL_H_INC

#include <sys/stat.h>
#include <fcntl.h>

#ifndef WIN32
	#include <termios.h>
#endif

#include "config.h"
#include "control.h"
#include "buffers.h"
#include "jack_playfile.h"

#ifndef WIN32
	static struct termios term_initial_settings; //cooked
	struct termios term_settings; //raw

	//lower values mean faster repetition of events from held key (~ ?)
	#define MAGIC_MAX_CHARS 5
	static unsigned char keycodes[ MAGIC_MAX_CHARS ];
#else
	DWORD        w_term_mode;
	HANDLE       w_term_hstdin;
	INPUT_RECORD w_term_inrec;
	DWORD        w_term_count;
#endif

//keys used to control jack_playfile
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
static int KEY_1=0;
static int KEY_2=0;
static int KEY_3=0;
static int KEY_4=0;
static int KEY_5=0;
static int KEY_6=0;
static int KEY_7=0;
static int KEY_8=0;
static int KEY_9=0;
static int KEY_J=0;
static int KEY_LT=0; //<
static int KEY_GT=0; //>

static const char *clear_to_eol_seq=NULL;
static const char *turn_on_cursor_seq=NULL;
static const char *turn_off_cursor_seq=NULL;

//for displaying 'wheel' as progress indicator
static int wheel_state=0;

static void kb_print_keyboard_shortcuts();
static void kb_handle_key_hits();
static void kb_print_clock();
static void kb_print_next_wheel_state(int direction);
static void kb_set_terminal_raw();
static int kb_read_raw_key();
static void kb_reset_terminal();
static void kb_init_term_seq();
static void kb_init_key_codes();

//=============================================================================
static void kb_print_keyboard_shortcuts()
{
	fprintf(stderr,"\r%s\r\n",clear_to_eol_seq);
	fprintf(stderr,"HELP\n\n");
	fprintf(stderr,"keyboard shortcuts:\n\n");

	fprintf(stderr,"  h, f1              help (this screen)\n");
	fprintf(stderr,"  space              toggle play/pause\n");
	fprintf(stderr,"  enter              play\n");
	fprintf(stderr,"  arrow left         seek one step backward\n");
	fprintf(stderr,"  arrow right        seek one step forward\n");
	fprintf(stderr,"  arrow up           increase seek step size\n");
	fprintf(stderr,"  arrow down         decrease seek step size\n");
	fprintf(stderr,"  home               seek to start\n");
	fprintf(stderr,"  0                  seek to start and pause\n");
	fprintf(stderr,"  backspace          seek to start and play\n");
	fprintf(stderr,"  end                seek to end\n");
#ifndef WIN32
	fprintf(stderr,"  < less than        load previous file\n");
	fprintf(stderr,"  > greater than     load next file\n");
#else
	fprintf(stderr,"  page up            load previous file\n");
	fprintf(stderr,"  page down          load next file\n");
#endif
	fprintf(stderr,"  1                  reset volume (zero amplification)\n");
	fprintf(stderr,"  2                  decrease volume\n");
	fprintf(stderr,"  3                  increase volume\n");
	fprintf(stderr,"  m                  toggle mute on/off*\n");
	fprintf(stderr,"  l                  toggle loop on/off*\n");
	fprintf(stderr,"  p                  toggle pause at end on/off*\n");
	fprintf(stderr,"  j                  toggle JACK transport on/off*\n");
	fprintf(stderr,"  c                  toggle clock display on*/off\n");
	fprintf(stderr,"  , comma            toggle clock seconds* /frames\n");
	fprintf(stderr,"  . period           toggle clock absolute*/relative\n");
	fprintf(stderr,"  - dash             toggle clock elapsed* /remaining\n");
	fprintf(stderr,"  q                  quit\n\n");

	fprintf(stderr,"prompt:\n\n");
	fprintf(stderr,"|| paused   JMALP  S rel 0.001       943.1  (00:15:43.070)  \n");
	fprintf(stderr,"^           ^^^^^  ^ ^   ^     ^     ^     ^ ^             ^\n");
	fprintf(stderr,"1           23456  7 8   9     10    11   10 12           13\n\n");
	fprintf(stderr,"  1): status playing > paused || or seeking ...\n");
	fprintf(stderr,"  2): JACK transport on/off J or ' '\n");
	fprintf(stderr,"  3): mute on/off M or ' '\n");
	fprintf(stderr,"  4): amplification A, ! (clipping) or ' ' (no amp.)\n");
	fprintf(stderr,"  5): loop on/off L or ' '\n");
	fprintf(stderr,"  6): pause at end on/off P or ' '\n");
	fprintf(stderr,"  7): time and seek in seconds S or frames F\n");
	fprintf(stderr,"  8): time indication rel to frame_offset or abs\n");
	fprintf(stderr,"  9): seek step size in seconds or frames\n");
	fprintf(stderr," 10): time elapsed ' ' or remaining -\n");
	fprintf(stderr," 11): time in seconds or frames\n");
	fprintf(stderr," 12): time in HMS.millis\n");
	fprintf(stderr," 13): keyboard input indication (i.e. seek)\n\n");

	//need command to print current props (file, offset etc)
}//end print_keyboard_shortcuts()

//=============================================================================
static void kb_handle_key_hits()
{
	int rawkey=kb_read_raw_key();
//	fprintf(stderr,"rawkey: %d\n",rawkey);

	//no key while timeout not reached
	if(rawkey==0)
	{
		;;//skip
	}
	//'space': toggle play/pause
	else if(rawkey==KEY_SPACE)
	{
		if(ctrl_toggle_play()==2)
		{
			fprintf(stderr,"idle at end");
		}
	}
	//'enter': play
	else if(rawkey==KEY_ENTER)
	{
		if(ctrl_play()==2)
		{
			fprintf(stderr,"idle at end");
		}
	}
	//'q': quit
	else if(rawkey==KEY_Q)
	{
		fprintf(stderr,"\r%s\rquit received\n",clear_to_eol_seq);
		ctrl_quit();
	}
	//'h' or 'f1': help
	else if(rawkey==KEY_H || rawkey==KEY_F1)
	{
		kb_print_keyboard_shortcuts();
	}
	//'<' (arrow left): 
	else if(rawkey==KEY_ARROW_LEFT)
	{
		fprintf(stderr,"<< ");
		kb_print_next_wheel_state(-1);
		ctrl_seek_backward();
	}
	//'>' (arrow right): 
	else if(rawkey==KEY_ARROW_RIGHT)
	{
		if(ctrl_seek_forward()==2)
		{
			fprintf(stderr,"idle at end");
		}
		else
		{
			kb_print_next_wheel_state(+1);
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
		ctrl_seek_start();
	}
	//'|< && >' (backspace):
	else if(rawkey==KEY_BACKSPACE)
	{
		fprintf(stderr,"|< home play");
		ctrl_seek_start_play();
	}
	//'|< && ||' (0):
	else if(rawkey==KEY_0)
	{
		fprintf(stderr,"|< home pause");
		settings->is_playing=0;
		ctrl_seek_start_pause();
	}
	//'>|' (end):
	else if(rawkey==KEY_END)
	{
		if(ctrl_seek_end()==2)
		{
			fprintf(stderr,"idle at end");
		}
	}
	//'m': toggle mute
	else if(rawkey==KEY_M)
	{
		ctrl_toggle_mute();
		fprintf(stderr,"mute %s",settings->is_muted ? "on " : "off ");
	}
	//'l': loop
	else if(rawkey==KEY_L)
	{
		ctrl_toggle_loop();
		fprintf(stderr,"loop %s",settings->loop_enabled ? "on " : "off ");
	}
	//'p': pause at end
	else if(rawkey==KEY_P)
	{
		ctrl_toggle_pause_at_end();
		fprintf(stderr,"pae %s",settings->pause_at_end ? "on " : "off ");
	}
	//',':  toggle seconds/frames
	else if(rawkey==KEY_COMMA)
	{
		settings->is_time_seconds=!settings->is_time_seconds;
		fprintf(stderr,"time %s",settings->is_time_seconds ? "seconds " : "frames ");

		if(settings->is_time_seconds)
		{
			set_seconds_from_exponent();
		}
		else
		{
			set_frames_from_exponent();
		}
	}
	//'-': toggle elapsed/remaining
	else if(rawkey==KEY_DASH)
	{
		settings->is_time_elapsed=!settings->is_time_elapsed;
		fprintf(stderr,"time %s",settings->is_time_elapsed ? "elapsed " : "remaining ");
	}
	//'.': toggle abs / rel
	else if(rawkey==KEY_PERIOD)
	{
		settings->is_time_absolute=!settings->is_time_absolute;
		fprintf(stderr,"time %s",settings->is_time_absolute ? "abs " : "rel ");
	}
	//'c': toggle clock on/off
	else if(rawkey==KEY_C)
	{
		settings->is_clock_displayed=!settings->is_clock_displayed;
		fprintf(stderr,"clock %s", settings->is_clock_displayed ? "on " : "off ");
	}
	//'j': toggle JACK transport on/off
	else if(rawkey==KEY_J)
	{
		ctrl_toggle_jack_transport();
		fprintf(stderr,"transport %s", jack->use_transport ? "on " : "off ");
	}
	//'<' load prev file
	else if(rawkey==KEY_LT)
	{
		ctrl_load_prev_file();
		fprintf(stderr,"prev file");
	}
	//'>' load next file
	else if(rawkey==KEY_GT)
	{
		ctrl_load_next_file();
		fprintf(stderr,"next file");
	}
	//'2' decrement volume
	else if(rawkey==KEY_2)
	{
		ctrl_decrement_volume();
		fprintf(stderr,"%.1f dBFS",jack->volume_amplification_decibel);
	}
	//'3' decrement volume
	else if(rawkey==KEY_3)
	{
		ctrl_increment_volume();
		fprintf(stderr,"%.1f dBFS",jack->volume_amplification_decibel);
	}
	//'1' reset volume
	else if(rawkey==KEY_1)
	{
		ctrl_reset_volume();
		fprintf(stderr,"volume reset");
	}
#ifndef WIN32
		fprintf(stderr,"%s",clear_to_eol_seq);
#else
		fprintf(stderr,"                 ");
#endif

}//end handle_key_hits()

//=============================================================================
static void kb_print_clock()
{
	if(!settings->is_clock_displayed)
	{
		return;
	}
/*
|---------------------------------|    file                                sr1
       |----------------------|        offset+count
              |-----------|   loop    in interleaved or resampled buffer   sr2
              v           v
              playhead    read pos

              |---------------.-----------------------| problem: more than one loop cycle in buffer

              
seek = buffer reset, stats reset
derive file pos from last seek pos (sr1) and playout count (sr2)
*/

/*
 elapsed                     remaining
|--------------------------|------------------|  abs

               elapsed       rem
             |-------------|-----|
                           v
|------------|-------------------|------------|  rel
             off                 off+count
*/

	sf_count_t pos=get_current_play_position_in_file();
	double seconds=0;

	if(!settings->is_time_absolute)
	{
		pos-=running->frame_offset;

		if(!settings->is_time_elapsed)
		{
			pos=running->frame_count-pos;
		}
	}
	else //absolute
	{
		if(!settings->is_time_elapsed)
		{
			pos=sf_info_generic.frames-pos;
		}
	}

	seconds=sin_frames_to_seconds(pos,sf_info_generic.sample_rate);

	if(settings->is_time_seconds)
	{
		if(seek_seconds_per_hit<1)
		{
			fprintf(stderr,"S %s %.3f %s %9.1f %s(%s) "
				,(settings->is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(settings->is_time_elapsed ? " " : "-")
				,seconds
				,(settings->is_time_elapsed ? " " : "-")
				,sin_format_duration_str(seconds));
		}
		else
		{
			fprintf(stderr,"S %s %5.0f  %s %9.1f %s(%s) "
				,(settings->is_time_absolute ? "abs" : "rel")
				,seek_seconds_per_hit
				,(settings->is_time_elapsed ? " " : "-")
				,seconds
				,(settings->is_time_elapsed ? " " : "-")
				,sin_format_duration_str(seconds));
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
			,(settings->is_time_absolute ? "abs" : "rel")
			,seek_fph
			,scale
			,(settings->is_time_elapsed ? " " : "-")
			,pos
			,(settings->is_time_elapsed ? " " : "-")
			,sin_format_duration_str(seconds));
	}
}// end print_clock()

//=============================================================================
//-1: counter clockwise 1: clockwise
static void kb_print_next_wheel_state(int direction)
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
static void kb_set_terminal_raw()
{
	if(!settings->keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//save original tty settings ("cooked")
	tcgetattr( STDIN_FILENO, &term_initial_settings );

	tcgetattr( STDIN_FILENO, &term_settings );

	//set the console mode to no-echo, raw input
	term_settings.c_cc[ VTIME ] = 1;
	term_settings.c_cc[ VMIN  ] = MAGIC_MAX_CHARS;
	term_settings.c_iflag &= ~(IXOFF);
	term_settings.c_lflag &= ~(ECHO | ICANON);
	tcsetattr( STDIN_FILENO, TCSANOW, &term_settings );

	//turn off cursor
	//fprintf(stderr,"%s",turn_off_cursor_seq);//now in jack_playfile.c

	//in shutdown signal handler
	//tcsetattr( STDIN_FILENO, TCSANOW, &term_initial_settings );
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
static int kb_read_raw_key()
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
	//do ReadConsoleInput( w_term_hstdin, &w_term_inrec, 1, &w_term_count );
	//while ((w_term_inrec.EventType != KEY_EVENT) || !w_term_inrec.Event.KeyEvent.bKeyDown);

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
static void kb_reset_terminal()
{
	if(!settings->keyboard_control_enabled)
	{
		return;
	}

#ifndef WIN32
	//reset terminal to original settings
	tcsetattr( STDIN_FILENO, TCSANOW, &term_initial_settings );
#endif
}

//=============================================================================
static void kb_init_term_seq()
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
static void kb_init_key_codes()
{
	if(!settings->keyboard_control_enabled)
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
	KEY_1=49;
	KEY_2=50;
	KEY_3=51;
	KEY_4=52;
	KEY_5=53;
	KEY_6=54;
	KEY_7=55;
	KEY_8=56;
	KEY_9=57;
	KEY_J=106;
	KEY_LT=60;
	KEY_GT=62;
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
	KEY_C=67;
	KEY_P=80;
	KEY_ENTER=13;
	KEY_0=48;
	KEY_1=49;
	KEY_2=50;
	KEY_3=51;
	KEY_4=52;
	KEY_5=53;
	KEY_6=54;
	KEY_7=55;
	KEY_8=56;
	KEY_9=57;
	KEY_J=74;
	KEY_LT=33;//hack
	KEY_GT=34;//hack
//	KEY_PAGE_UP=33;
//	KEY_PAGE_DOWN=34;
#endif
}//init_key_codes()

#endif
//EOF
