/** @file rb.h \mainpage
 * rb.h -- A set of functions to work with lock-free ringbuffers.
 *
 * \image html drawing_ringbuffer_1_path.svg "Ringbuffer"
 * \image latex drawing_ringbuffer_1_path.eps "Ringbuffer"
 *
 * The key attribute of this ringbuffer is that it can be safely accessed
 * by two threads simultaneously -- one reading from the buffer and
 * the other writing to it -- without using any synchronization or
 * mutual exclusion primitives. For this to work correctly, there can
 * only be a single reader and a single writer thread. Their
 * identities cannot be interchanged, i.e. a reader can not become a
 * writer and vice versa.
 *
 * Please find a documentation of all functions here: \ref rb.h.
 * 
 * rb.h is part of a collection of C snippets which can be found here:
 * [https://github.com/7890/csnip](https://github.com/7890/csnip)
 *
 * Copyright (C) 2015 Thomas Brand
 *
 * rb.h is derived from ringbuffer.c and ringbuffer.h in jack repository
 * [https://github.com/jackaudio/jack1](https://github.com/jackaudio/jack1)
 *
 * Any errors or shortcomings in this derivative code shall not be related to 
 * the authors this file (rb.h) was based on.
 *
 * Header from ringbuffer.c (jack_ringbuffer_t):
 * @code
 * Copyright (C) 2000 Paul Davis
 * Copyright (C) 2003 Rohan Drape
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * ISO/POSIX C version of Paul Davis's lock free ringbuffer C++ code.
 * This is safe for the case of one read thread and one write thread.
 * @endcode
 */

//EXPERIMENTAL CODE
//NOT ALL METHODS FULLY TESTED

#ifndef _RB_H
#define _RB_H

#ifdef __cplusplus
extern "C" {
#endif

//#define RB_DISABLE_MLOCK

/**< If defined (without value), do NOT provide POSIX memory locking (see rb_mlock(), rb_munlock()).*/

//#define RB_DISABLE_RW_MUTEX

/**< If defined (without value), do NOT provide read and write mutex locks.
Programs that include "rb.h" without setting RB_DISABLE_RW_MUTEX defined need to link with '-lphtread'.
Not disabling doesn't mean that read and write operations are locked by default.
A caller can use these methods to wrap read and write operations:
See also rb_try_exclusive_read(), rb_release_read(), rb_try_exclusive_write(), rb_release_write(). */

//#define RB_DISABLE_SHM

/**< If defined (without value), do NOT provide shared memory support.
Files created in shared memory can normally be found under '/dev/shm/'.
Programs that include "rb.h" without setting RB_DISABLE_SHM need to link with '-lrt -luuid'.
See also rb_new_shared(). */

//#define RB_DEFAULT_USE_SHM
/**< If defined (without value), rb_new() will implicitely use shared memory backed storage.
Otherwise rb_new() will use malloc(), in private heap storage.
See also rb_new_shared(). */
//#endif

#include <stdlib.h> //malloc, free
#include <string.h> //memcpy
#include <sys/types.h> //size_t
#include <stdio.h> //fprintf

#include <inttypes.h> //uint8_t

#ifndef RB_DISABLE_MLOCK
	#include <sys/mman.h> //mlock, munlock
#endif
#ifndef RB_DISABLE_RW_MUTEX
	#include <pthread.h> //pthread_mutex_init, pthread_mutex_lock ..
#endif
#ifndef RB_DISABLE_SHM
	#include <sys/mman.h> //mmap
	#include <unistd.h> //ftruncate
	#include <fcntl.h> //constants O_CREAT, ..
	#include <sys/stat.h> //
	#include <sys/shm.h> //shm_open, shm_unlink, mmap
	#include <uuid/uuid.h> //uuid_generate_time_safe
#endif

/**
 * Ringbuffers are of type rb_t.
 * 
 * The members of an rb_t struct must normally not be changed directly by a user of rb.h.
 * However it can be done, this is an open system. Not using the rb_* functions to operate
 * on rb_t will produce arbitrary results.
 *
 * Example use:
 * @code
 * rb_t *ringbuffer;
 * ringbuffer=rb_new(1024);
 * if(ringbuffer!=NULL) { //do stuff }
 * @endcode
 */
typedef struct
{
  size_t size;			/**< \brief The size in bytes of the buffer as requested by caller. */
  volatile size_t read_index;	/**< \brief Absolute position (index) in the buffer for read operations. */
  volatile size_t write_index;	/**< \brief Abolute position (index) in the buffer for write operations. */
  volatile int last_was_write;	/**< \brief Whether or not the last operation on the buffer was of type write (write index advanced).
				      !last_was_write corresponds to read operation accordingly (read pinter advanced). */
  int memory_locked;		/**< \brief Whether or not the buffer is locked to memory (if locked, no virtual memory disk swaps). */
  int in_shared_memory;		/**< \brief Whether or not the buffer is allocated as a file in shared memory (normally found under '/dev/shm'.)*/
  char shm_handle[256];		/**< \brief Name of shared memory file, alphanumeric handle. */

#ifndef RB_DISABLE_RW_MUTEX
  pthread_mutexattr_t mutex_attributes;
  pthread_mutex_t read_lock;		/**< \brief Mutex lock for mutually exclusive read operations. */
  pthread_mutex_t write_lock;		/**< \brief Mutex lock for mutually exclusive write operations. */
#endif
}
rb_t;

//make struct memebers accessible via function
static inline int rb_is_mlocked(rb_t *rb) {return rb->memory_locked;}
static inline int rb_is_shared(rb_t *rb) {return rb->in_shared_memory;}
static inline size_t rb_size(rb_t *rb){return rb->size;}
static inline char *rb_get_shared_memory_handle(rb_t *rb) {return rb->shm_handle;}

/**
 * Read and write regions are of type rb_region_t.
 * 
 * A region is a continuous part in the ringbuffer, defined through start (pointer) and length (size).
 * 
 * See rb_get_read_regions(), rb_get_write_regions(), rb_get_next_read_region(), rb_get_next_write_region().
 *
 * Example use:
 * @code
 * rb_region_t regions[2];
 * rb_get_read_regions(rb,regions);
 * if(regions[0].size>0) { //do stuff with regions[0].buffer, first part of possible split }
 * if(regions[1].size>0) { //do stuff with regions[1].buffer, second part of possible split }
 * @endcode
 */
typedef struct  
{
  char  *buffer;	/**< \brief Pointer to location in main byte buffer. It correponds to a partial (segment)
			      or full area of the main byte buffer in rb_t. */
  size_t size;		/**< \brief Count of bytes that can be read from the buffer. */
} 
rb_region_t;

static inline rb_t *rb_new(size_t size);
static inline rb_t *rb_new_shared(size_t size);
static inline void rb_free(rb_t *rb);
static inline int rb_mlock(rb_t *rb);
static inline int rb_munlock(rb_t *rb);
static inline int rb_is_mlocked(rb_t *rb);
static inline int rb_is_shared(rb_t *rb);
static inline size_t rb_size(rb_t *rb);
static inline char *rb_get_shared_memory_handle(rb_t *rb);
static inline void rb_reset(rb_t *rb);
static inline size_t rb_can_read(const rb_t *rb);
static inline size_t rb_can_write(const rb_t *rb);
static inline size_t rb_read(rb_t *rb, char *destination, size_t count);
static inline size_t rb_write(rb_t *rb, const char *source, size_t count);
static inline size_t rb_peek(const rb_t *rb, char *destination, size_t count);
static inline size_t rb_peek_at(const rb_t *rb, char *destination, size_t count, size_t offset);
static inline size_t rb_drop(rb_t *rb);
static inline int rb_find_byte(rb_t *rb, char byte, size_t *offset);
static inline int rb_find_byte_sequence(rb_t *rb, char *pattern, size_t pattern_offset, size_t count, size_t *offset);
static inline size_t rb_read_byte(rb_t *rb, char *destination);
static inline size_t rb_peek_byte(const rb_t *rb, char *destination);
static inline size_t rb_peek_byte_at(const rb_t *rb, char *destination, size_t offset);
static inline size_t rb_skip_byte(rb_t *rb);
static inline size_t rb_write_byte(rb_t *rb, const char *source);
static inline int rb_find_next_midi_message(rb_t *rb, size_t *offset, size_t *count);
static inline size_t rb_read_float(rb_t *rb, float *destination);
static inline size_t rb_peek_float(const rb_t *rb, float *destination);
static inline size_t rb_peek_float_at(const rb_t *rb, float *destination, size_t offset);
static inline size_t rb_skip_float(rb_t *rb);
static inline size_t rb_write_float(rb_t *rb, const float *source);
static inline size_t rb_advance_read_index(rb_t *rb, size_t count);
static inline size_t rb_advance_write_index(rb_t *rb, size_t count);
static inline void rb_get_read_regions(const rb_t *rb, rb_region_t *regions);
static inline void rb_get_write_regions(const rb_t *rb, rb_region_t *regions);
static inline void rb_get_next_read_region(const rb_t *rb, rb_region_t *region);
static inline void rb_get_next_write_region(const rb_t *rb, rb_region_t *region);
static inline int rb_try_exclusive_read(rb_t *rb);
static inline void rb_release_read(rb_t *rb);
static inline int rb_try_exclusive_write(rb_t *rb);
static inline void rb_release_write(rb_t *rb);
static inline void *buf_ptr(const rb_t *rb);
static inline void rb_debug(rb_t *rb);
static inline void rb_print_regions(rb_t *rb);

/**
 * Allocate a ringbuffer data structure of a specified size. The
 * caller must arrange for a call to rb_free() to release
 * the memory associated with the ringbuffer after use.
 *
 * The ringbuffer is allocated in heap memory with malloc() unless 
 * RB_DEFAULT_USE_SHM=1 is set at compile time in which case rb_new_shared()
 * will be called implicitely.
 *
 * @param size the ringbuffer size in bytes, >0
 *
 * @return a pointer to a new rb_t if successful; NULL otherwise.
 */
//=============================================================================
static inline rb_t *rb_new(size_t size)
{
#ifndef RB_DISABLE_SHM
	#ifdef RB_DEFAULT_USE_SHM
		return rb_new_shared(size);
	#endif
#endif
	if(size<1) {return NULL;}

	rb_t *rb;

	//malloc space for rb_t struct and buffer
	rb=(rb_t*)malloc(sizeof(rb_t) + size); //
	if(rb==NULL) {return NULL;}

	//the attached buffer is in the same malloced space
	//right after rb_t (at offset sizeof(rb_t))

	rb->size=size;
	rb->write_index=0;
	rb->read_index=0;
	rb->last_was_write=0;	
	rb->memory_locked=0;
	rb->in_shared_memory=0;

#ifndef RB_DISABLE_RW_MUTEX
	pthread_mutex_init ( &rb->read_lock, NULL);
	pthread_mutex_init ( &rb->write_lock, NULL);
#endif
	return rb;
}

/**
 * Allocate a ringbuffer data structure of a specified size.
 *
 * The ringbuffer is allocated as a file in shared memory with a name similar to
 * 'b6310884-9938-11e5-bf8c-74d435e313ae'. Shared memory is normally visible in the
 * filesystem under /dev/shm/. The generated name ought to be unique (uuid) and 
 * can be accessed in rb_t as member ->shm_handle. 
 *
 * Ringbuffers allocated in shared memory can be accessed by other processes.
 *
 * Not calling rb_free() after use means to leave a file in /dev/shm/.
 * This can can be intentional or not.
 *
 * Inspecting a ringbuffer in /dev/shm/ for debug purposes can be done from a
 * shell using i.e. hexdump.
 *
 * Example to see what's going on in a small buffer:
 *
 * @code
 *	watch -d -n 0.1 hexdump -c /dev/shm/b6310884-9938-11e5-bf8c-74d435e313ae
 * @endcode
 *
 * @param size the ringbuffer size in bytes, >0
 *
 * @return a pointer to a new rb_t if successful; NULL otherwise.
 */
//=============================================================================
static inline rb_t *rb_new_shared(size_t size)
{
#ifdef RB_DISABLE_SHM
	return NULL;
#else
	if(size<1) {return NULL;}

	rb_t *rb;

	//create rb_t in shared memory
	uuid_t uuid;
	uuid_generate_time_safe(uuid);
	char shm_handle[37]; //i.e. "b6310884-9938-11e5-bf8c-74d435e313ae" + "\0"
	uuid_unparse_lower(uuid, shm_handle);

	//O_TRUNC |
	int fd=shm_open(shm_handle,O_CREAT | O_RDWR, 0666);
	if(fd<0) {return NULL;}

	int r=ftruncate(fd,sizeof(rb_t) + size);
	if(r!=0) {return NULL;}

	//void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	rb=(rb_t*)mmap(0, sizeof(rb_t) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if(rb==NULL || rb==MAP_FAILED) {return NULL;}
//	fprintf(stderr,"rb address %lu\n",(unsigned long int)rb);
	memcpy(rb->shm_handle,shm_handle,37);
//	fprintf(stderr,"buffer address %lu\n",(unsigned long int)buf_ptr(rb));

	rb->size=size;
	rb->write_index=0;
	rb->read_index=0;
	rb->last_was_write=0;	
	rb->memory_locked=0;
	rb->in_shared_memory=1;

#ifndef RB_DISABLE_RW_MUTEX
	pthread_mutexattr_init(&rb->mutex_attributes);
	pthread_mutexattr_setpshared(&rb->mutex_attributes, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&rb->read_lock, &rb->mutex_attributes);
	pthread_mutex_init(&rb->write_lock, &rb->mutex_attributes);
#endif
	return rb;
#endif
}

/**
 * Open an existing ringbuffer data structure in shared memory.
 *
 * The ringbuffer to open must be available as a file in shared memory.
 * Shared memory is normally visible in the filesystem under /dev/shm/.
 *
 * Not calling rb_free() after use means to leave a file in /dev/shm/. This can be
 * intentional or not.
 *
 * Inspecting a ringbuffer in /dev/shm/ for debug purposes can be done from a
 * shell using i.e. hexdump.
 *
 * Example to see what's going on in a small buffer:
 *
 *@code
 *watch -d -n 0 hexdump -c /dev/shm/b6310884-9938-11e5-bf8c-74d435e313ae
 *@endcode
 *
 * @param shm_handle a name (uuid) identifying the ringbuffer (without leading path) to open
 *
 * @return a pointer to a new rb_t if successful; NULL otherwise.
 */
//=============================================================================
static inline rb_t *rb_open_shared(const char *shm_handle)
{
#ifdef RB_DISABLE_SHM
	return NULL;
#else
	rb_t *rb;

	//create rb_t in shared memory
	uuid_t uuid;

	//O_TRUNC | O_CREAT | 
	int fd=shm_open(shm_handle,O_RDWR, 0666);
	if(fd<0) {return NULL;}

//??
//	int r=ftruncate(fd,sizeof(rb_t));
//	if(r!=0) {return NULL;}

	//void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	rb=(rb_t*)mmap(0, sizeof(rb_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if(rb==NULL || rb==MAP_FAILED) {return NULL;}

	fprintf(stderr,"size %zu\n ",rb->size);
	size_t size=rb->size;

	//unmap and remap fully (knowing size now)
	munmap(rb,sizeof(rb_t));
	rb=(rb_t*)mmap(0, sizeof(rb_t) + size , PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if(rb==NULL || rb==MAP_FAILED) {return NULL;}

	fprintf(stderr,"rb address %lu\n",(unsigned long int)rb);
	fprintf(stderr,"buffer address %lu\n",(unsigned long int)buf_ptr(rb));

	return rb;
#endif
}

/**
 * Free the ringbuffer data structure allocated by an earlier call to rb_new().
 *
 * Any active reader and/or writer should be done before calling rb_free().
 *
 * @param rb a pointer to the ringbuffer structure.
 */
//=============================================================================
static inline void rb_free(rb_t *rb)
{
	if(rb==NULL) {return;}
#ifndef RB_DISABLE_MLOCK
	if(rb->memory_locked)
	{
		munlock(rb, sizeof(rb_t) + rb->size);
	}
#endif
	rb->size=0;
#ifndef RB_DISABLE_SHM
	if(rb->in_shared_memory)
	{
		shm_unlink(rb->shm_handle);
		return;
	}
#endif
	free(rb);
}

/**
 * Lock a ringbuffer data block into memory.
 *
 * Uses the mlock() system call.
 * 
 * This is not a realtime operation.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return 1 if successful; 0 otherwise.
 */
//=============================================================================
static inline int rb_mlock(rb_t *rb)
{
#ifndef RB_DISABLE_MLOCK
	if(mlock(rb, sizeof(rb_t) + rb->size)) {return 0;}
	rb->memory_locked=1;
	return 1;
#else
	return 0;
#endif
}

/**
 * Unlock a previously locked ringbuffer data block in memory.
 *
 * Uses the munlock() system call.
 * 
 * This is not a realtime operation.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return 1 if successful; 0 otherwise.
 */
//=============================================================================
static inline int rb_munlock(rb_t *rb)
{
#ifndef RB_DISABLE_MLOCK
	if(munlock(rb, sizeof(rb_t) + rb->size)) {return 0;}
	rb->memory_locked=0;
	return 1;
#else
	return 0;
#endif
}

/**
 * Reset the read and write indices, making an empty buffer.
 *
 * Any active reader and/or writer should be done before calling rb_reset().
 *
 * @param rb a pointer to the ringbuffer structure.
 */
//=============================================================================
static inline void rb_reset(rb_t *rb)
{
	rb->read_index=0;
	rb->write_index=0;
	rb->last_was_write=0;
}

/**
 * Return the number of bytes available for reading.
 *
 * This is the number of bytes in front of the read index up to the write index.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the number of bytes available to read.
 */
//=============================================================================
static inline size_t rb_can_read(const rb_t *rb)
{
	size_t r=rb->read_index;
	size_t w=rb->write_index;

	if(r==w)
	{
		if(rb->last_was_write) {return rb->size;}
		else {return 0;}
	}
	else if(r<w) {return w-r;}
	else {return w+rb->size-r;} //r>w
}

/**
 * Return the number of bytes available for writing.
 *
 * This is the number of bytes in front of the write index up to the read index.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the amount of free space (in bytes) available for writing.
 */
//=============================================================================
static inline size_t rb_can_write(const rb_t *rb)
{
	size_t r=rb->read_index;
	size_t w=rb->write_index;

	if(r==w)
	{
		if(rb->last_was_write) {return 0;}
		else {return rb->size;}
	}
	else if(r<w) {return rb->size-w+r;}
	else {return r-w;} //r>w
}

/**
 * Read data from the ringbuffer and move the read index accordingly.
 * This is a copying data reader.
 *
 * The count of bytes effectively read can be less than the requested
 * count, which is limited to rb_can_read() bytes.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a buffer where data read from the
 * ringbuffer will be copied to.
 * @param count the number of bytes to read.
 *
 * @return the number of bytes read, which may range from 0 to count.
 */
//=============================================================================
static inline size_t rb_read(rb_t *rb, char *destination, size_t count)
{
	if(count==0) {return 0;}
	size_t can_read_count;
	//can not read more than offset, no chance to read from there
	if(!(can_read_count=rb_can_read(rb))) {return 0;}
	size_t do_read_count=count>can_read_count ? can_read_count : count;
	size_t linear_end=rb->read_index+do_read_count;
	size_t copy_count_1;
	size_t copy_count_2;

	if(linear_end>rb->size)
	{
		copy_count_1=rb->size-rb->read_index;
		copy_count_2=linear_end-rb->size;
	}
	else
	{
		copy_count_1=do_read_count;
		copy_count_2=0;
	}

	memcpy(destination, &( ((char*)buf_ptr(rb)) [rb->read_index] ), copy_count_1);

	if(!copy_count_2)
	{
		rb->read_index=(rb->read_index+copy_count_1) % rb->size;
	}
	else
	{
		memcpy(destination+copy_count_1, &( ((char*)buf_ptr(rb)) [0]), copy_count_2);

		rb->read_index=copy_count_2 % rb->size;
	}
	rb->last_was_write=0;
	return do_read_count;
}

/**
 * Write data into the ringbuffer and move the write index accordingly.
 * This is a copying data writer.
 *
 * The count of bytes effectively written can be less than the requested
 * count, which is limited to rb_can_write() bytes.
 * 
 * @param rb a pointer to the ringbuffer structure.
 * @param source a pointer to the data to be written to the ringbuffer.
 * @param count the number of bytes to write.
 *
 * @return the number of bytes written, which may range from 0 to count
 */
//=============================================================================
static inline size_t rb_write(rb_t *rb, const char *source, size_t count)
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
	linear_end=rb->write_index+do_write_count;

	if(linear_end>rb->size)
	{
		copy_count_1=rb->size-rb->write_index;
		copy_count_2=linear_end-rb->size;
	}
	else
	{
		copy_count_1=do_write_count;
		copy_count_2=0;
	}

	memcpy( &(  ((char*)buf_ptr(rb))  [rb->write_index] ), source, copy_count_1);

	if(!copy_count_2)
	{
		rb->write_index=(rb->write_index+copy_count_1) % rb->size;
	}
	else
	{
		memcpy( &(  ((char*)buf_ptr(rb))  [0]), source+copy_count_1, copy_count_2);
		rb->write_index=copy_count_2 % rb->size;
	}
	rb->last_was_write=1;
	return do_write_count;
}

/**
 * Read data from the ringbuffer. Opposed to rb_read()
 * this function does not move the read index. Thus it's
 * a convenient way to inspect data in the ringbuffer in a
 * continuous fashion. The data is copied into a buffer provided by the caller.
 * For non-copy inspection of the data in the ringbuffer
 * use rb_get_read_regions() or rb_get_next_read_region().
 *
 * The count of bytes effectively read can be less than the requested
 * count, which is limited to rb_can_read() bytes.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a buffer where data read from the
 * ringbuffer will be copied to.
 * @param count the number of bytes to read.
 *
 * @return the number of bytes read, which may range from 0 to count.
 */
//=============================================================================
static inline size_t rb_peek(const rb_t *rb, char *destination, size_t count)
{
	return rb_peek_at(rb,destination,count,0);
}

/**
 * Read data from the ringbuffer. Opposed to rb_read()
 * this function does not move the read index. Thus it's
 * a convenient way to inspect data in the ringbuffer in a
 * continuous fashion. The data is copied into a buffer provided by the caller.
 * For non-copy inspection of the data in the ringbuffer
 * use rb_get_read_regions() or rb_get_next_read_region().
 *
 * The count of bytes effectively read can be less than the requested
 * count, which is limited to rb_can_read() bytes.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a buffer where data read from the
 * ringbuffer will be copied to.
 * @param count the number of bytes to read.
 * @param offset the number of bytes to skip at the start of readable ringbuffer data.
 *
 * @return the number of bytes read, which may range from 0 to count.
 */
//=============================================================================
static inline size_t rb_peek_at(const rb_t *rb, char *destination, size_t count, size_t offset)
{
	if(count==0) {return 0;}
	size_t can_read_count;
	//can not read more than offset, no chance to read from there
	if((can_read_count=rb_can_read(rb))<=offset) {return 0;}
	//limit read count respecting offset
	size_t do_read_count=count>can_read_count-offset ? can_read_count-offset : count;
	//adding the offset, knowing it could be beyond buffer end
	size_t tmp_read_index=rb->read_index+offset;
	//including all: current read index + offset + limited read count
	size_t linear_end=tmp_read_index+do_read_count;
	size_t copy_count_1;
	size_t copy_count_2;

	//beyond
	if(linear_end>rb->size)
	{
		//still beyond
		if(tmp_read_index>=rb->size)
		{
			//all in rolled over
			tmp_read_index%=rb->size;
			copy_count_1=do_read_count;
			copy_count_2=0;
		}
		//segmented
		else
		{
			copy_count_1=rb->size-tmp_read_index;
			copy_count_2=linear_end-rb->size-offset;
		}
	}
	else
	//if not beyond the buffer end
	{
		copy_count_1=do_read_count;
		copy_count_2=0;
	}
	memcpy(destination, &(  ((char*)buf_ptr(rb))  [tmp_read_index]), copy_count_1);

	if(copy_count_2)
	{
		memcpy(destination+copy_count_1, &(  ((char*)buf_ptr(rb))  [0]), copy_count_2);
	}
	return do_read_count;
}

/**
 * Drop / ignore all data available to read.
 * This moves the read index up to the current write index
 * (nothing left to read) using rb_advance_read_index().
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the number of bytes dropped, which may range from 0 to rb->size.
 */
//=============================================================================
static inline size_t rb_drop(rb_t *rb)
{
	return rb_advance_read_index(rb,rb_can_read(rb));
}

/**
 * Search for a given byte in the ringbuffer's readable space.
 * The index at which the byte was found is copied to the
 * offset variable provided by the caller. The index is relative to
 * the start of the ringbuffer's readable space.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param byte the byte to search in the ringbuffer's readable space
 * @param offset a pointer to a size_t variable
 *
 * @return 1 if found; 0 otherwise.
 */
//=============================================================================
static inline int rb_find_byte(rb_t *rb, char byte, size_t *offset)
{
	size_t off=0;
	char c;
	while(rb_peek_byte_at(rb,&c,off))
	{
		if(c==byte)
		{
			memcpy(offset,&(off),sizeof(size_t)); //hmmm.
			return 1;
		}
		off++;
	}
	off=0;
	memcpy(offset,&(off),sizeof(size_t));
	return 0;
}

/**
 * Read one byte from the ringbuffer.
 * This advances the read index by one byte if at least one byte
 * is available in the ringbuffer's readable space.
 *
 * This is a copying data reader.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the byte read from the
 * ringbuffer will be copied to.
 *
 * @return the number of bytes read, which may range from 0 to 1.
 */
//=============================================================================
static inline size_t rb_read_byte(rb_t *rb, char *destination)
{
	return rb_read(rb,destination,1);
}

/**
 * Peek one byte from the ringbuffer (don't move the read index).
 * This is a copying data reader.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the byte read from the
 * ringbuffer will be copied to.
 *
 * @return the number of bytes read, which may range from 0 to 1.
 */
//=============================================================================
static inline size_t rb_peek_byte(const rb_t *rb, char *destination)
{
	return rb_peek_byte_at(rb,destination,0);
}

/**
 * Peek one byte from the ringbuffer at the given offset (don't move the read index).
 * This is a copying data reader.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the byte read from the
 * ringbuffer will be copied to.
 * @param offset the number of bytes to skip at the start of readable ringbuffer data.
 *
 * @return the number of bytes read, which may range from 0 to 1.
 */
//=============================================================================
static inline size_t rb_peek_byte_at(const rb_t *rb, char *destination, size_t offset)
{
	size_t can_read_count;
	if((can_read_count=rb_can_read(rb))<=offset) {return 0;}

	size_t tmp_read_index=rb->read_index+offset;

	if(rb->size<=tmp_read_index)
	{
		memcpy(destination, &(  ((char*)buf_ptr(rb))  [tmp_read_index-rb->size]),1);
	}
	else
	{
		memcpy(destination, &(  ((char*)buf_ptr(rb))  [tmp_read_index]),1);
	}
}

/**
 * Drop / ignore one byte available to read.
 * This advances the read index by one byte if at least one byte
 * is available in the ringbuffer's readable space.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the number of bytes skipped, which may range from 0 to 1.
 */
//=============================================================================
static inline size_t rb_skip_byte(rb_t *rb)
{
	return rb_advance_read_index(rb,1);
}

/**
 * Write one byte to the ringbuffer.
 * This advances the write index by one byte if at least one byte
 * can be written to the ringbuffer's writable space.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param source a pointer to the variable containing the byte to be written
 * to the ringbuffer.
 *
 * @return the number of bytes written, which may range from 0 to 1.
 */
//=============================================================================
static inline size_t rb_write_byte(rb_t *rb, const char *source)
{
	return rb_write(rb,source,1);
}

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

/**
 * Read sizeof(float) bytes from the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the float value read from the
 * ringbuffer will be copied to.
 *
 * @return the number of bytes read, which may be 0 or sizeof(float).
 */
//=============================================================================
static inline size_t rb_read_float(rb_t *rb, float *destination)
{
	if(rb_can_read(rb)>=sizeof(float))
	{
		return rb_read(rb,(char*)destination,sizeof(float));
	}
	return 0;
}

/**
 * Read sizeof(float) bytes from the ringbuffer. Opposed to rb_read_float()
 * this function does not move the read index.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the float value read from the
 * ringbuffer will be copied to.
 *
 * @return the number of bytes read, which may be 0 or sizeof(float).
 */
//=============================================================================
static inline size_t rb_peek_float(const rb_t *rb, float *destination)
{
	if(rb_can_read(rb)>=sizeof(float))
	{
		return rb_peek(rb,(char*)destination,sizeof(float));
	}
	return 0;
}

/**
 * Read sizeof(float) bytes from the ringbuffer at a given offset. Opposed to
 * rb_read_float() this function does not move the read index.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a variable where the float value read from the
 * ringbuffer will be copied to.
 * @param offset the number of bytes to skip at the start of readable ringbuffer data.
 *
 * @return the number of bytes read, which may be 0 or sizeof(float).
 */
//=============================================================================
static inline size_t rb_peek_float_at(const rb_t *rb, float *destination, size_t offset)
{
	size_t can_read_count;
	if((can_read_count=rb_can_read(rb))<offset+sizeof(float)) {return 0;}

	return rb_peek_at(rb,(char*)destination,sizeof(float),offset);
}

/**
 * Move the read index by sizeof(float) bytes.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the number of bytes skipped, which may be 0 or sizeof(float).
 */
//=============================================================================
static inline size_t rb_skip_float(rb_t *rb)
{
	if(rb_can_read(rb)>=sizeof(float))
	{
		return rb_advance_read_index(rb,sizeof(float));
	}
	return 0;
}

/**
 * Write sizeof(float) bytes to the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param source a pointer to the variable containing the float value to be written
 * to the ringbuffer.
 *
 * @return the number of bytes written, which may be 0 or sizeof(float).
 */
//=============================================================================
static inline size_t rb_write_float(rb_t *rb, const float *source)
{
	if(rb_can_write(rb)>=sizeof(float))
	{
		return rb_write(rb,(char*)source,sizeof(float));
	}
	return 0;
}

/**
 * Advance the read index.
 *
 * After data have been read from the ringbuffer using the pointers
 * returned by rb_get_read_regions() or rb_get_next_read_region(), use this
 * function to advance the read index, making that space available for 
 * future write operations.
 *
 * The count of the read index advance can be less than the requested
 * count, which is limited to rb_can_read() bytes.
 *
 * Advancing the read index without prior reading the involved bytes 
 * means dropping/ignoring data.
 * 
 * @param rb a pointer to the ringbuffer structure.
 * @param count the number of bytes to advance.
 *
 * @return the number of bytes advanced, which may range from 0 to count.
 */
//=============================================================================
static inline size_t rb_advance_read_index(rb_t *rb, size_t count)
{
	if(count==0) {return 0;}
	size_t can_read_count;
	if(!(can_read_count=rb_can_read(rb))) {return 0;}

	size_t do_advance_count=count>can_read_count ? can_read_count : count;
	size_t r=rb->read_index;
	size_t linear_end=r+do_advance_count;
	size_t tmp_read_index=linear_end>rb->size ? linear_end-rb->size : r+do_advance_count;

	rb->read_index=(tmp_read_index%=rb->size);
	rb->last_was_write=0;
	return do_advance_count;
}

/**
 * Advance the write index.
 *
 * After data have been written to the ringbuffer using the pointers
 * returned by rb_get_write_regions() or rb_get_next_write_region() use this
 * function to advance the write index, making the data available for
 * future read operations.
 *
 * The count of the write index advance can be less than the requested
 * count, which is limited to rb_can_write() bytes.
 *
 * Advancing the write index without prior writing the involved bytes 
 * means leaving arbitrary data in the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param count the number of bytes to advance.
 *
 * @return the number of bytes advanced, which may range from 0 to count
 */
//=============================================================================
static inline size_t rb_advance_write_index(rb_t *rb, size_t count)
{
	if(count==0) {return 0;}
	size_t can_write_count;
	if(!(can_write_count=rb_can_write(rb))) {return 0;}

	size_t do_advance_count=count>can_write_count ? can_write_count : count;
	size_t w=rb->write_index;
	size_t linear_end=w+do_advance_count;
	size_t tmp_write_index=linear_end>rb->size ? linear_end-rb->size : w+do_advance_count;

	rb->write_index=(tmp_write_index%=rb->size);
	rb->last_was_write=1;
	return do_advance_count;
}

/**
 * Fill a data structure with a description of the current readable
 * data held in the ringbuffer. This description is returned in a two
 * element array of rb_region_t. Two elements are needed
 * because the data to be read may be split across the end of the
 * ringbuffer.
 *
 * The first element will always contain a valid @a len field, which
 * may be zero or greater. If the @a len field is non-zero, then data
 * can be read in a continuous fashion using the address given in the
 * corresponding @a buf field.
 *
 * If the second element has a non-zero @a len field, then a second
 * continuous stretch of data can be read from the address given in
 * its corresponding @a buf field.
 * 
 * This method allows to read data from the ringbuffer directly using
 * pointers to the ringbuffer's readable spaces.
 *
 * If data was read this way, the caller must manually advance the read
 * index accordingly using rb_advance_read_index().
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param regions a pointer to a 2 element array of rb_region_t.
 *
 */
//=============================================================================
static inline void rb_get_read_regions(const rb_t *rb, rb_region_t *regions)
{
	size_t can_read_count=rb_can_read(rb);
	size_t r=rb->read_index;
	size_t linear_end=r+can_read_count;

	if(linear_end>rb->size)
	{
		// Two part vector: the rest of the buffer after the current write
		// index, plus some from the start of the buffer.
		regions[0].buffer=&(  ((char*)buf_ptr(rb))  [r]);
		regions[0].size=rb->size-r;
		regions[1].buffer=  ((char*)buf_ptr(rb));
		regions[1].size=linear_end-rb->size;
	}
	else
	{
		// Single part vector: just the rest of the buffer
		regions[0].buffer=&(  ((char*)buf_ptr(rb))  [r]);
		regions[0].size=can_read_count;
		regions[1].size=0;
	}
}

/**
 * Fill a data structure with a description of the current writable
 * space in the ringbuffer. The description is returned in a two
 * element array of rb_region_t. Two elements are needed
 * because the space available for writing may be split across the end
 * of the ringbuffer.
 *
 * The first element will always contain a valid @a len field, which
 * may be zero or greater. If the @a len field is non-zero, then data
 * can be written in a continuous fashion using the address given in
 * the corresponding @a buf field.
 *
 * If the second element has a non-zero @a len field, then a second
 * continuous stretch of data can be written to the address given in
 * the corresponding @a buf field.
 *
 * This method allows to write data to the ringbuffer directly using
 * pinters to the ringbuffer's writable spaces.
 *
 * If data was written this way, the caller must manually advance the write
 * index accordingly using rb_advance_write_index().
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param regions a pointer to a 2 element array of rb_region_t.
 */
//=============================================================================
static inline void rb_get_write_regions(const rb_t *rb, rb_region_t *regions)
{
	size_t can_write_count=rb_can_write(rb);
	size_t w=rb->write_index;
	size_t linear_end=w+can_write_count;

	if(linear_end>rb->size)
	{
		// Two part vector: the rest of the buffer after the current write
		// index, plus some from the start of the buffer.
		regions[0].buffer=&(  ((char*)buf_ptr(rb))  [w]);
		regions[0].size=rb->size-w;
		regions[1].buffer=  ((char*)buf_ptr(rb));
		regions[1].size=linear_end-rb->size;
	}
	else
	{
		// Single part vector: just the rest of the buffer
		regions[0].buffer=&(  ((char*)buf_ptr(rb))  [w]);
		regions[0].size=can_write_count;
		regions[1].size=0;
	}
}

/**
* This function is similar to rb_get_write_regions().
* Opposed to rb_get_write_regions(), it will only return the first (next) region
* instead of an array of two regions. The region is returned to the caller by 
* setting the rb_region_t variable provided by the caller.
* If data was read using the provided pointer and size in rb_region_t, the read
* index must be manually advanced using rb_advance_read_index().
*
* @param rb a pointer to the ringbuffer structure.
* @param region a pointer to a variable of type rb_region_t.
*/
//=============================================================================
static inline void rb_get_next_read_region(const rb_t *rb, rb_region_t *region)
{
	size_t can_read_count=rb_can_read(rb);
	size_t r=rb->read_index;
	size_t linear_end=r+can_read_count;

	if(linear_end>rb->size)
	{
		region->buffer=&(  ((char*)buf_ptr(rb))  [r]);
		region->size=rb->size-r;
	}
	else
	{
		region->buffer=&(  ((char*)buf_ptr(rb))  [r]);
		region->size=can_read_count;
	}
}
/**
* This function is similar to rb_get_read_regions().
* Opposed to rb_get_read_regions(), it will only return the first (next) region
* instead of an array of two regions. The region is returned to the caller by 
* setting the rb_region_t variable provided by the caller.
* If data was written using the provided pointer and size in rb_region_t, the write
* index must be manually advanced using rb_advance_write_index().
*
* @param rb a pointer to the ringbuffer structure.
* @param region a pointer to a variable of type rb_region_t.
*/
//=============================================================================
static inline void rb_get_next_write_region(const rb_t *rb, rb_region_t *region)
{
	size_t can_write_count=rb_can_write(rb);
	size_t w=rb->write_index;
	size_t linear_end=w+can_write_count;

	if(linear_end>rb->size)
	{
		region->buffer=&(  ((char*)buf_ptr(rb))  [w]);
		region->size=rb->size-w;
	}
	else
	{
		region->buffer=&(  ((char*)buf_ptr(rb))  [w]);
		region->size=can_write_count;
	}
}

/**
 * Search for a given byte sequence in the ringbuffer's readable space.
 * The index at which the byte sequence was found is copied to the
 * offset variable provided by the caller. The index is relative to
 * the start of the ringbuffer's readable space.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param pattern the byte sequence to search in the ringbuffer's readable space
 * @param pattern_offset the offset to apply to the byte sequence
 * @param count the count of bytes to consider for matching in the byte sequence, starting from offset
 * @param offset a pointer to a size_t variable
 *
 * @return 1 if found; 0 otherwise.
 */
//=============================================================================
static inline int rb_find_byte_sequence(rb_t *rb, char *pattern, size_t pattern_offset, size_t count, size_t *offset)
{
	char *tmp_pattern=pattern;
	tmp_pattern+=pattern_offset;

	char compare_buffer[count];
	size_t off=0;
	char c;
	while(rb_peek_byte_at(rb,&c,off))
	{
		if(c==tmp_pattern[0])//found start
		{
			//read to compare buffer
			if(rb_peek_at(rb,compare_buffer,count,off)==count)
			{
				if(!memcmp(tmp_pattern,compare_buffer,count))
				{
					memcpy(offset,&(off),sizeof(size_t)); //hmmm.
					return 1;
				}
			}
			else {goto _not_found;}
		}
		off++;
	}
_not_found:
	off=0;
	memcpy(offset,&(off),sizeof(size_t));
	return 0;
}

/**
 * Internal method to handle pointer indirections.
 * A user of rb.h must not call this function directly.
 * 
 * Every process that attaches a common shared memory region with mmap() will get a different pointer value (address).
 * 
 * A pointer in a shared datastructure in shared memory to another chunk of memory can be problematic:
 *
 * -Every process accessing the main shared structure must also have access to the referenced resource.
 *
 * -Processes that memory map the referenced chunk will get a different pointer value (address).
 *
 * -> A common pointer in shared memory will point to an invalid address except for the process that created it.
 * 
 * This problem can be solved by using a known common base and an offset.
 *
 * Every process that has a pointer to the start of an rb_t can easily calculate the pointer
 * to the buffer by offsetting it by sizeof(rb_t) since the buffer is allocated in the same chunk of memory
 * and attached directly after rb_t members. buf_ptr() does just that.
 *
 * @code
 *
 *->.------------- start of allocated space == start of rb_t
 *  |
 *  | rb_t "header"
 *  |
 *->|------ end of rb_t == start of buffer
 *  |
 *  | buffer (size bytes)
 *  |
 *  |
 *  |
 *  .------------- end of allocated space == end of buffer
 *
 * Total size = sizeof(rb_t) + size
 * @endcode
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return a pointer to the data buffer of this rb_t.
 */
static inline void *buf_ptr(const rb_t *rb)
{
	return (char*)rb+sizeof(rb_t);
}

/**
* Try to lock the ringbuffer for exclusive read.
* Only one process can lock the ringbuffer for reading at a time.
* Other processes can not read from the ringbuffer while it is locked.
* A process that successfully locks the ringbuffer must arrange for a call
* to rb_release_read() when the lock is not needed any longer.
*
* @param rb a pointer to the ringbuffer structure.
*
* @return 1 if locked; 0 otherwise.
*/
//=============================================================================
static inline int rb_try_exclusive_read(rb_t *rb)
{
#ifndef RB_DISABLE_RW_MUTEX
	if(pthread_mutex_trylock(&rb->read_lock)) {return 0;}
	else {return 1;}
#else
	return 0;
#endif
}

/**
* Release a previously acquired write lock with rb_try_exclusive_write().
*
* Only processes that successfully locked the ringbuffer previously are allowed to call this function.
*
* @param rb a pointer to the ringbuffer structure.
*/
//=============================================================================
static inline void rb_release_read(rb_t *rb)
{
#ifndef RB_DISABLE_RW_MUTEX
	pthread_mutex_unlock(&rb->read_lock);
#endif
}

/**
* Try to lock the ringbuffer for exclusive write.
* Only one process can lock the ringbuffer for reading at a time.
* Other processes can not read from the ringbuffer while it is locked.
* A process that successfully locks the ringbuffer must arrange for a call
* to rb_release_write() when the lock is not needed any longer.
*
* @param rb a pointer to the ringbuffer structure.
*
* @return 1 if locked; 0 otherwise.
*/
//=============================================================================
static inline int rb_try_exclusive_write(rb_t *rb)
{
#ifndef RB_DISABLE_RW_MUTEX
	if(pthread_mutex_trylock(&rb->write_lock)) {return 0;}
	else {return 1;}
#else
	return 0;
#endif
}

/**
* Release a previously acquired write lock with rb_try_exclusive_write().
*
* Only processes that successfully locked the ringbuffer previously are allowed to call this function.
*
* @param rb a pointer to the ringbuffer structure.
*/
//=============================================================================
static inline void rb_release_write(rb_t *rb)
{
#ifndef RB_DISABLE_RW_MUTEX
	pthread_mutex_unlock(&rb->write_lock);
#endif
}

/**
* Print out information about ringbuffer to stderr.
*/
//=============================================================================
void rb_debug(rb_t *rb)
{
	if(rb==NULL)
	{
		fprintf(stderr,"rb is NULL\n");
		return;
	}
	fprintf(stderr,"can read: %zu @ %zu  can write: %zu @ %zu last was: %s mlock: %s shm: %s %s\n"
		,rb_can_read(rb)
		,rb->read_index
		,rb_can_write(rb)
		,rb->write_index
		,rb->last_was_write ? "write." : "read."
		,rb->memory_locked ? "yes." : "no."
		,rb->in_shared_memory ? "yes." : "no."
		,rb->in_shared_memory ? rb->shm_handle : "malloc()"
	);
}

/**
* Print out information about rinbuffer regions to stderr.
*/
//=============================================================================
void rb_print_regions(rb_t *rb)
{
	rb_region_t data[2];
	rb_get_read_regions(rb,data);
	fprintf(stderr,"read region size  %zu %zu =%zu  "
		,data[0].size
		,data[1].size
		,data[0].size+data[1].size
	);

	rb_get_write_regions(rb,data);
	fprintf(stderr,"write region size %zu %zu =%zu\n"
		,data[0].size
		,data[1].size
		,data[0].size+data[1].size
	);
}

//=============================================================================
//"ALIASES"

/**
* \brief This is an alias to rb_advance_read_index().
*/
static inline size_t rb_skip(rb_t *rb, size_t count) {return rb_advance_read_index(rb,count);}

/**
* \brief This is an alias to rb_drop().
*/
static inline size_t rb_skip_all(rb_t *rb, size_t count) {return rb_drop(rb);}

//#define RB_ALIASES_1
#ifdef RB_ALIASES_1
//if rb.h is used as a jack_ringbuffer replacement these wrappers simplify source modification
//(sed 's/jack_ringbuffer_/rb_/g')
static inline rb_t *rb_create(size_t size)			{return rb_new(size);}
static inline size_t rb_read_space(const rb_t *rb)		{return rb_can_read(rb);}
static inline size_t rb_write_space(const rb_t *rb)		{return rb_can_write(rb);}
static inline size_t rb_read_advance(rb_t *rb, size_t count)	{return rb_advance_read_index(rb,count);}
static inline size_t rb_write_advance(rb_t *rb, size_t count)	{return rb_advance_write_index(rb,count);}
static inline void rb_get_read_vector(const rb_t *rb, rb_region_t *regions) {return rb_get_read_regions(rb,regions);}
static inline void rb_get_write_vector(const rb_t *rb, rb_region_t *regions) {return rb_get_write_regions(rb,regions);}
#endif

//#define RB_ALIASES_2
#ifdef RB_ALIASES_2
//inspired by https://github.com/xant/libhl/blob/master/src/rbuf.c,
//http://svn.drobilla.net/lad/trunk/raul/raul/RingBuffer.hpp
static inline size_t rb_capacity(rb_t *rb)	{return rb->size;}
static inline void rb_clear(rb_t *rb)		{rb_reset(rb);}
static inline void rb_destroy(rb_t *rb)		{rb_free(rb);}
#endif

#ifdef __cplusplus
}
#endif

#endif //header guard
//EOF
