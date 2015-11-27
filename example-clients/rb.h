/*
  Copyright (C) 2000 Paul Davis
  Copyright (C) 2003 Rohan Drape
  Copyright (C) 2015 Thomas Brand

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software 
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  ISO/POSIX C version of Paul Davis's lock free ringbuffer C++ code.
  This is safe for the case of one read thread and one write thread.
*/

//tb/151126
// rb.h is derived from ringbuffer.c and ringhbuffer.h in jack repository
// https://github.com/jackaudio/jack1

// this file (rb.h) is part of a collection of c snipets
// https://github.com/7890/csnip
// refined for generic use (i.e. no JACK-specific naming), all-in-one .h file
// allow arbitrary sized ringbuffers
//    size must not necessariliy be a power of two
//    buffer size will normally be exactly like requested
//    if buffer size=n: _can_write==n after creation
// methods and types renamed:
// jack_ringbuffer_* -> rb_*
// _create -> _new
// _read_space -> _can_read, _write_space -> _can_write
// _read_advance -> _advance_read_pointer, _write_advance -> _advance_write_pointer
// advance methods return count (limited to can_read/can_write)
// new methods:
// _drop: advance read pointer up to write pointer
// _get_next_read_vector, _get_next_write_vector

#ifndef _RB_H
#define _RB_H

#ifdef __cplusplus
extern "C" {
#endif

//Use POSIX memory locking
#define RB_USE_MLOCK 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RB_USE_MLOCK
	#include <sys/mman.h>
#endif
#include <sys/types.h>

typedef struct  
{
  char  *buffer;
  size_t size;
} 
rb_data_t ;

typedef struct
{
  char *buffer;
  volatile size_t read_pointer;
  volatile size_t write_pointer;
  size_t size;
  int memory_locked;
  volatile int last_was_write; //
} 
rb_t ;

rb_t *rb_new(size_t size); // _create
void rb_free(rb_t *rb);
int rb_mlock(rb_t *rb);
void rb_reset(rb_t *rb);
size_t rb_can_read(const rb_t *rb); // _read_space
size_t rb_can_write(const rb_t *rb); // _write_space
size_t rb_read(rb_t *rb, char *destination, size_t count);
size_t rb_peek(rb_t *rb, char *destination, size_t count);
size_t rb_drop(rb_t *rb); //
size_t rb_write(rb_t *rb, const char *source, size_t count);
size_t rb_advance_read_pointer(rb_t *rb, size_t count); //_read_advance
size_t rb_advance_write_pointer(rb_t *rb, size_t count); //_write_advance
void rb_get_read_vector(const rb_t *rb, rb_data_t *vec);
void rb_get_write_vector(const rb_t *rb, rb_data_t *vec);
void rb_get_next_read_vector(const rb_t *rb, rb_data_t *vec); //
void rb_get_next_write_vector(const rb_t *rb, rb_data_t *vec); //

/*
rules in this ring:
-read pointer can never pass write pointer
-write pointer can never pass read pointer
-read and write pointer can be at the same place

both r and w at 0:
r
-------
w

ambiguous. possible meanings:
A) cannot write (0),  max read  (size)
B) cannot read  (0) , max write (size)
-> need to know last pointer movement

if last was w:
r
------
 ????w
-> case A

if last was r:
 ????r
------
w
->case B

  r   w
----------
  ....
can read: w-r
can write: size-(w-r)

  w   r
----------
      .... size-r
.. + w
can read: w+size-r
can write r-w
*/

// Create a new ringbuffer to hold exactly 'size' bytes of data.
//=============================================================================
rb_t *rb_new(size_t size)
{
	if(size<1)
	{
		return NULL;
	}

	rb_t *rb;
	
	rb=(rb_t*)malloc(sizeof(rb_t));
	if(rb==NULL)
	{
		return NULL;
	}

	rb->size=size;
	rb->write_pointer=0;
	rb->read_pointer=0;

	rb->buffer=(char*)malloc(rb->size);
	if(rb->buffer==NULL)
	{
		free(rb);
		return NULL;
	}
	rb->memory_locked=0;
	rb->last_was_write=0;	

	return rb;
}

// Free all data associated with the ringbuffer 'rb'. 
//=============================================================================
void rb_free(rb_t *rb)
{
#ifdef RB_USE_MLOCK
	if(rb->memory_locked)
	{
		munlock(rb->buffer, rb->size);
	}
#endif
	rb->size=0;
	free(rb->buffer);
	free(rb);
}

// Lock the data block of 'rb' using the system call 'mlock'.
//=============================================================================
int rb_mlock(rb_t *rb)
{
#ifdef RB_USE_MLOCK
	if(mlock(rb->buffer, rb->size))
	{
		return -1;
	}
#endif
	rb->memory_locked=1;
	return 0;
}

// Reset the read and write pointers to zero. This is not thread safe.
//=============================================================================
void rb_reset(rb_t *rb)
{
	rb->read_pointer=0;
	rb->write_pointer=0;
	rb->last_was_write=0;
}

// Return the number of bytes available for reading. This is the number of bytes 
// in front of the read pointer and behind the write pointer.
//=============================================================================
size_t rb_can_read(const rb_t *rb)
{
	size_t r=rb->read_pointer;
	size_t w=rb->write_pointer;

	if(r==w)
	{
		if(rb->last_was_write)
		{
			return rb->size;
		}
		else
		{
			return 0;
		}
	}
	else if(r<w)
	{
		return w-r;
	}
	else //r>w
	{
		return w+rb->size-r;
	}
}

// Return the number of bytes available for writing. This is the number of bytes 
// in front of the write pointer and behind the read pointer.
//=============================================================================
size_t rb_can_write(const rb_t *rb)
{
	size_t r=rb->read_pointer;
	size_t w=rb->write_pointer;

	if(r==w)
	{
		if(rb->last_was_write)
		{
			return 0;
		}
		else
		{
			return rb->size;
		}
	}
	else if(r<w)
	{
		return rb->size-w+r;
	}
	else //r>w
	{
		return r-w;
	}
}

// The copying data reader.  Copy at most 'count' bytes from 'rb' to 'destination'. 
// Returns the actual number of bytes copied.
//=============================================================================
size_t rb_read(rb_t *rb, char *destination, size_t count)
{
	size_t can_read_count;
	size_t do_read_count;
	size_t linear_end;
	size_t copy_count_1;
	size_t copy_count_2;

	if(!(can_read_count=rb_can_read(rb)))
	{
		return 0;
	}

	do_read_count=count>can_read_count ? can_read_count : count;
	linear_end=rb->read_pointer+do_read_count;

	if(linear_end>rb->size)
	{
		copy_count_1=rb->size-rb->read_pointer;
		copy_count_2=linear_end-rb->size;
	}
	else
	{
		copy_count_1=do_read_count;
		copy_count_2=0;
	}

	memcpy(destination, &(rb->buffer[rb->read_pointer]), copy_count_1);

	if(!copy_count_2)
	{
		rb->read_pointer=(rb->read_pointer+copy_count_1) % rb->size; ///////
	}
	else
	{
		memcpy(destination+copy_count_1, &(rb->buffer[0]), copy_count_2);
		rb->read_pointer=copy_count_2 % rb->size;
	}
	rb->last_was_write=0;
	return do_read_count;
}

// The copying data reader w/o read pointer advance. Copy at most 'count' bytes 
// from 'rb' to 'destination'.  Returns the actual number of bytes copied.
//=============================================================================
size_t rb_peek(rb_t *rb, char *destination, size_t count)
{
	size_t can_read_count;
	size_t do_read_count;
	size_t linear_end;
	size_t copy_count_1;
	size_t copy_count_2;
	size_t tmp_read_pointer;

	tmp_read_pointer=rb->read_pointer;

	if(!(can_read_count=rb_can_read(rb)))
	{
		return 0;
	}

	do_read_count=count>can_read_count ? can_read_count : count;
	linear_end=tmp_read_pointer+do_read_count;

	if(linear_end>rb->size)
	{
		copy_count_1=rb->size-tmp_read_pointer;
		copy_count_2=linear_end-rb->size;
	}
	else
	{
		copy_count_1=do_read_count;
		copy_count_2=0;
	}

	memcpy(destination, &(rb->buffer[tmp_read_pointer]), copy_count_1);

	if(copy_count_2)
	{
		tmp_read_pointer=0;
		memcpy(destination+copy_count_1, &(rb->buffer[tmp_read_pointer]), copy_count_2);
	}
	return do_read_count;
}

// Drop / ignore all data available to read. This moves the read pointer to the current
// write pointer (nothing left to read).
//=============================================================================
size_t rb_drop(rb_t *rb)
{
	return rb_advance_read_pointer(rb,rb_can_read(rb));
}

// The copying data writer.  Copy at most 'count' bytes to 'rb' from 'source'. 
// Returns the actual number of bytes copied.
//=============================================================================
size_t rb_write(rb_t *rb, const char *source, size_t count)
{
	size_t can_write_count;
	size_t do_write_count;
	size_t linear_end;
	size_t copy_count_1;
	size_t copy_count_2;

	if(!(can_write_count=rb_can_write(rb)))
	{
		return 0;
	}

	do_write_count=count>can_write_count ? can_write_count : count;
	linear_end=rb->write_pointer+do_write_count;

	if(linear_end>rb->size)
	{
		copy_count_1=rb->size-rb->write_pointer;
		copy_count_2=linear_end-rb->size;
	}
	else
	{
		copy_count_1=do_write_count;
		copy_count_2=0;
	}

	memcpy(&(rb->buffer[rb->write_pointer]), source, copy_count_1);

	if(!copy_count_2)
	{
		rb->write_pointer=(rb->write_pointer+copy_count_1) % rb->size;
	}
	else
	{
		memcpy(&(rb->buffer[0]), source+copy_count_1, copy_count_2);
		rb->write_pointer=copy_count_2 % rb->size;
	}
	rb->last_was_write=1;
	return do_write_count;
}

// Advance the read pointer by 'count' bytes. 'count' is limited to can_read().
//=============================================================================
size_t rb_advance_read_pointer(rb_t *rb, size_t count)
{
	size_t can_read_count=rb_can_read(rb);
	size_t do_advance_count=count>can_read_count ? can_read_count : count;
	size_t r=rb->read_pointer;
	size_t linear_end=r+do_advance_count;
	size_t tmp_read_pointer;

	if(linear_end>rb->size)
	{
		tmp_read_pointer=linear_end-rb->size;
	}
	else
	{
		tmp_read_pointer=r+do_advance_count;
	}
	tmp_read_pointer%=rb->size;
	rb->read_pointer=tmp_read_pointer;
	rb->last_was_write=0;
	return do_advance_count;
}

// Advance the write pointer by 'count' bytes.
//=============================================================================
size_t rb_advance_write_pointer(rb_t *rb, size_t count)
{
	size_t can_write_count=rb_can_write(rb);
	size_t do_advance_count=count>can_write_count ? can_write_count : count;
	size_t w=rb->write_pointer;
	size_t linear_end=w+do_advance_count;
	size_t tmp_write_pointer;

	if(linear_end>rb->size)
	{
		tmp_write_pointer=linear_end-rb->size;
	}
	else
	{
		tmp_write_pointer=w+do_advance_count;
	}
	tmp_write_pointer%=rb->size;
	rb->write_pointer=tmp_write_pointer;
	rb->last_was_write=1;
	return do_advance_count;
}

// The non-copying data reader. 'vec' is an array of two places.  Set the values 
// at 'vec' to hold the current readable data at 'rb'.  If the readable data is 
// in one segment the second segment has zero length.
//=============================================================================
void rb_get_read_vector(const rb_t *rb, rb_data_t *vec)
{
	size_t can_read_count=rb_can_read(rb);
	size_t r=rb->read_pointer;
	size_t linear_end=r+can_read_count;

	if(linear_end>rb->size)
	{
		// Two part vector: the rest of the buffer after the current write
		// pointer, plus some from the start of the buffer.
		vec[0].buffer=&(rb->buffer[r]);
		vec[0].size=rb->size-r;
		vec[1].buffer=rb->buffer;
		vec[1].size=linear_end-rb->size;
	}
	else
	{
		// Single part vector: just the rest of the buffer
		vec[0].buffer=&(rb->buffer[r]);
		vec[0].size=can_read_count;
		vec[1].size=0;
	}
}

//=============================================================================
void rb_get_write_vector(const rb_t *rb, rb_data_t *vec)
{
	size_t can_write_count=rb_can_write(rb);
	size_t w=rb->write_pointer;
	size_t linear_end=w+can_write_count;

	if(linear_end>rb->size)
	{
		// Two part vector: the rest of the buffer after the current write
		// pointer, plus some from the start of the buffer.
		vec[0].buffer=&(rb->buffer[w]);
		vec[0].size=rb->size-w;
		vec[1].buffer=rb->buffer;
		vec[1].size=linear_end-rb->size;
	}
	else
	{
		// Single part vector: just the rest of the buffer
		vec[0].buffer=&(rb->buffer[w]);
		vec[0].size=can_write_count;
		vec[1].size=0;
	}
}

//=============================================================================
void rb_get_next_read_vector(const rb_t *rb, rb_data_t *vec)
{
	size_t can_read_count=rb_can_read(rb);
	size_t r=rb->read_pointer;
	size_t linear_end=r+can_read_count;

	if(linear_end>rb->size)
	{
		vec->buffer=&(rb->buffer[r]);
		vec->size=rb->size-r;
	}
	else
	{
		vec->buffer=&(rb->buffer[r]);
		vec->size=can_read_count;
	}
}

//=============================================================================
void rb_get_next_write_vector(const rb_t *rb, rb_data_t *vec)
{
	size_t can_write_count=rb_can_write(rb);
	size_t w=rb->write_pointer;
	size_t linear_end=w+can_write_count;

	if(linear_end>rb->size)
	{
		vec->buffer=&(rb->buffer[w]);
		vec->size=rb->size-w;
	}
	else
	{
		vec->buffer=&(rb->buffer[w]);
		vec->size=can_write_count;
	}
}

#ifdef __cplusplus
}
#endif

#endif
//EOF
