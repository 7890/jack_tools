/** @file rb_midi.h \mainpage
 * rb_midi.h is part of a collection of C snippets which can be found here:
 * [https://github.com/7890/csnip](https://github.com/7890/csnip)
 *
 * Copyright (C) 2015 Thomas Brand
 */

#ifndef _RB_MIDI_H
#define _RB_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rb.h"

static inline int rb_find_next_midi_message(rb_t *rb, size_t *offset, size_t *count);

/**
 * Find next MIDI message in the readable space of the ringbuffer.
 *
 * The offset at which a MIDI message can be found is returned
 * by setting the 'offset' variable provided by the caller.
 *
 * The length of the MIDI message is returned by setting the
 * 'count' variable provided by the caller.
 *
 * Valid MIDI messages have a length of one, two or three bytes.
 *
 * Custom length MIDI SySex messages are not supported at this time.
 *
 * If a MIDI message was found, a caller can then skip 'offset' bytes
 * and read 'count' bytes to get a complete MIDI message byte sequence.
 *
 * The following overview on MIDI messages can be found in its original form here:
 * https://ccrma.stanford.edu/~craig/articles/linuxmidi/misc/essenmidi.html.
 * 
 * MIDI commands and data are distinguished according to the most significant bit of the byte. 
 * If there is a zero in the top bit, then the byte is a data byte, and if there is a one 
 * in the top bit, then the byte is a command byte. Here is how they are separated: 
 *@code
 *     decimal     hexadecimal          binary
 * =======================================================
 * DATA bytes:
 *        0               0          00000000
 *      ...             ...               ...
 *      127              7F          01111111
 * 
 * COMMAND bytes:
 *      128              80          10000000
 *      ...             ...               ...
 *      255              FF          11111111
 *@endcode
 * Furthermore, command bytes are split into half. The most significant half contains the 
 * actual MIDI command, and the second half contains the MIDI channel for which the command 
 * is for. For example, 0x91 is the note-on command for the second MIDI channel. the 9 
 * digit is the actual command for note-on and the digit 1 specifies the second channel 
 * (the first channel being 0). The 0xF0 set of commands do not follow this convention. 
 *@code 
 *    0x80     Note Off
 *    0x90     Note On
 *    0xA0     Aftertouch
 *    0xB0     Continuous controller
 *    0xC0     Patch change
 *    0xD0     Channel Pressure
 *    0xE0     Pitch bend
 *    0xF0     (non-musical commands)
 *@endcode
 * The messages from 0x80 to 0xEF are called Channel Messages because the second four bits 
 * of the command specify which channel the message affects. 
 * The messages from 0xF0 to 0xFF are called System Messages; they do not affect any particular channel. 
 * 
 * A MIDI command plus its MIDI data parameters to be called a MIDI message.
 *
 * *The minimum size of a MIDI message is 1 byte (one command byte and no parameter bytes).*
 *
 * *The maximum size of a MIDI message (not considering 0xF0 commands) is three bytes.*
 *
 * A MIDI message always starts with a command byte. Here is a table of the MIDI messages 
 * that are possible in the MIDI protocol: 
 *@code 
 * Command Meaning                 # parameters    param 1         param 2
 * 0x80    Note-off                2               key             velocity
 * 0x90    Note-on                 2               key             veolcity
 * 0xA0    Aftertouch              2               key             touch
 * 0xB0    Control Change          2               controller #    controller value
 * 0xC0    Program Change          1               instrument #
 * 0xD0    Channel Pressure        1               pressure
 * 0xE0    Pitch bend              2               lsb (7 bits)    msb (7 bits)
 * 0xF0    (non-musical commands)
 *@endcode 
 * 
 * @param rb a pointer to the ringbuffer structure.
 * @param offset a pointer to a variable of type size_t.
 * @param count a pointer to a variable of type size_t.
 *
 * @return 1 if found; 0 otherwise.
 */
//=============================================================================
static inline int rb_find_next_midi_message(rb_t *rb, size_t *offset, size_t *count)
{
	size_t msg_len=0;
	size_t skip_counter=0;

	while(rb_can_read(rb)>skip_counter)
	{
		msg_len=0;
		char c;
		//read one byte
		rb_peek_byte_at(rb,&c,skip_counter);

		uint8_t type = c & 0xF0;

		if(type == 0x80 //off
			|| type == 0x90 //on
			|| type == 0xA0 //at
			|| type == 0xB0 //ctrl
			|| type == 0xE0 //pb
		)
		{
			msg_len=3;
		}
		else if(type == 0xC0 //pc
			|| type == 0xD0 //cp
		)
		{
			msg_len=2;
		}
		else if(type == 0xF0) //rt
		{
			msg_len=1;
		}

		if(msg_len==0)
		{
			//this is not a size byte. skip it on next read
			skip_counter++;
			//rb_advance_read_pointer(rb,1);
			continue;
		}

		if(rb_can_read(rb)>=skip_counter+msg_len)
		{
			memcpy(offset,&(skip_counter),sizeof(size_t));
			memcpy(count,&(msg_len),sizeof(size_t));
			return 1;
		}
		else
		{
			break;
		}
	}
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif //header guard
//EOF
