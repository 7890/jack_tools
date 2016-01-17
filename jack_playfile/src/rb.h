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
 * Copyright (C) 2015 - 2016 Thomas Brand
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

//the first few bytes in a rb_t data block
static const char RB_MAGIC[8]={'r','i','n','g','b','u','f','\0'};
//followed by version
static const float RB_VERSION=0.21;


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
#include <math.h> //ceil, floor

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

#ifndef MAX
	#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
	#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

static const char *bar_string="============================================================";

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
  char magic[8];
  float version;
  size_t size;			/**< \brief The size in bytes of the buffer as requested by caller. */
  volatile size_t read_index;	/**< \brief Absolute position (index) in the buffer for read operations. */
  volatile size_t write_index;	/**< \brief Abolute position (index) in the buffer for write operations. */
  volatile int last_was_write;	/**< \brief Whether or not the last operation on the buffer was of type write (write index advanced).
				      !last_was_write corresponds to read operation accordingly (read pinter advanced). */
  int memory_locked;		/**< \brief Whether or not the buffer is locked to memory (if locked, no virtual memory disk swaps). */
  int in_shared_memory;		/**< \brief Whether or not the buffer is allocated as a file in shared memory (normally found under '/dev/shm'.)*/

  int unlink_requested;		/**< \brief If set to 1, readers and writers should consider the buffer deleted, not using it anymore (unlinking it with rb_free())*/ 

  int no_more_input_data;	/**< \brief A writer can indicate that no more data will be put to the rinbuffer, i.e. the writer finished.*/

  int sample_rate;		/**< \brief If ringbuffer is used as audio buffer, sample_rate is > 0.*/
  int channel_count;		/**< \brief The number of channels stored in this buffer (interleaved).*/
  int bytes_per_sample;		/**< \brief The number of bytes per audio sample.*/

  char shm_handle[256];		/**< \brief Name of shared memory file, alphanumeric handle. */
  char human_name[256];		/**< \brief Name of buffer, alphanumeric. */

  uint64_t total_bytes_read;	/**< \brief Total bytes read from this ringbuffer. Aadvancing the read index is iterpreted as read. Internal calls are also counted. rb_reset() will reset this value. */
  uint64_t total_bytes_write;	/**< \brief Total bytes written to this ringbuffer. Advancing the write index is interpreted as write. Internal calls are also counted. rb_reset() will reset this value. */
  uint64_t total_bytes_peek;	/**< \brief Total bytes peeked from this ringbuffer. rb_reset() will reset this value. */
  uint64_t total_underflows;	/**< \brief Total underflow incidents (not bytes): could not read the requested amount of bytes. rb_reset() will reset this value. */
  uint64_t total_overflows;	/**< \brief Total overflow incidents (not bytes): could not write the requested amount of bytes. rb_reset() will reset this value. */

#ifndef RB_DISABLE_RW_MUTEX
  pthread_mutexattr_t mutex_attributes;
  pthread_mutex_t read_lock;		/**< \brief Mutex lock for mutually exclusive read operations. */
  pthread_mutex_t write_lock;		/**< \brief Mutex lock for mutually exclusive write operations. */
#endif
}
rb_t;

//make struct memebers accessible via function
static inline int rb_version(rb_t *rb) {return rb->version;}
static inline int rb_is_mlocked(rb_t *rb) {return rb->memory_locked;}
static inline int rb_is_shared(rb_t *rb) {return rb->in_shared_memory;}
static inline int rb_is_unlink_requested(rb_t *rb) {return rb->unlink_requested;}
static inline void rb_request_unlink(rb_t *rb) {rb->unlink_requested=1;}
static inline size_t rb_size(rb_t *rb){return rb->size;}
static inline char *rb_shared_memory_handle(rb_t *rb) {return rb->shm_handle;}
static inline char *rb_human_name(rb_t *rb) {return rb->human_name;}
static inline int rb_sample_rate(rb_t *rb) {return rb->sample_rate;}
static inline int rb_channel_count(rb_t *rb) {return rb->channel_count;}
static inline int rb_bytes_per_sample(rb_t *rb) {return rb->bytes_per_sample;}

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

static inline void rb_set_common_init_values(rb_t *rb);
static inline rb_t *rb_new(size_t size);
static inline rb_t *rb_new_named(size_t size, const char *name);
static inline rb_t *rb_new_audio(size_t size, const char *name, int sample_rate, int channel_count, int bytes_per_sample);
static inline rb_t *rb_new_audio_seconds(double seconds, const char *name, int sample_rate, int channel_count, int bytes_per_sample);
static inline rb_t *rb_new_shared(size_t size);
static inline rb_t *rb_new_shared_named(size_t size, const char *name);
static inline rb_t *rb_new_shared_audio(size_t size, const char *name, int sample_rate, int channel_count, int bytes_per_sample);
static inline rb_t *rb_new_shared_audio_seconds(double seconds, const char *name, int sample_rate, int channel_count, int bytes_per_sample);

static inline void rb_free(rb_t *rb);
static inline int rb_mlock(rb_t *rb);
static inline int rb_munlock(rb_t *rb);
static inline int rb_is_mlocked(rb_t *rb);
static inline int rb_is_shared(rb_t *rb);
static inline size_t rb_size(rb_t *rb);
static inline char *rb_get_shared_memory_handle(rb_t *rb);
static inline void rb_reset(rb_t *rb);
static inline size_t rb_can_read(const rb_t *rb);
static inline size_t rb_can_read_frames(const rb_t *rb);
static inline size_t rb_can_write(const rb_t *rb);
static inline size_t rb_can_write_frames(const rb_t *rb);
static inline size_t rb_generic_read(rb_t *rb, char *destination, size_t count, int over);
static inline size_t rb_read(rb_t *rb, char *destination, size_t count);
static inline size_t rb_overread(rb_t *rb, char *destination, size_t count);
static inline size_t rb_write(rb_t *rb, const char *source, size_t count);
static inline size_t rb_peek(rb_t *rb, char *destination, size_t count);
static inline size_t rb_peek_at(rb_t *rb, char *destination, size_t count, size_t offset);
static inline size_t rb_drop(rb_t *rb);
static inline int rb_find_byte(rb_t *rb, char byte, size_t *offset);
static inline int rb_find_byte_sequence(rb_t *rb, char *pattern, size_t pattern_offset, size_t count, size_t *offset);
static inline size_t rb_read_byte(rb_t *rb, char *destination);
static inline size_t rb_peek_byte(rb_t *rb, char *destination);
static inline size_t rb_peek_byte_at(rb_t *rb, char *destination, size_t offset);
static inline size_t rb_skip_byte(rb_t *rb);
static inline size_t rb_write_byte(rb_t *rb, const char *source);
static inline size_t rb_deinterleave_items(rb_t *rb, char *destination ,size_t item_count, size_t item_size, size_t initial_item_offset, size_t item_block_size);
static inline size_t rb_deinterleave_audio(rb_t *rb, char *destination ,size_t frame_count, size_t frame_offset);
static inline size_t rb_generic_advance_read_index(rb_t *rb, size_t count, int over);
static inline size_t rb_advance_read_index(rb_t *rb, size_t count);
static inline size_t rb_overadvance_read_index(rb_t *rb, size_t count);
static inline size_t rb_advance_write_index(rb_t *rb, size_t count);
static inline void rb_get_read_regions(const rb_t *rb, rb_region_t *regions);
static inline void rb_get_write_regions(const rb_t *rb, rb_region_t *regions);
static inline void rb_get_next_read_region(const rb_t *rb, rb_region_t *region);
static inline void rb_get_next_write_region(const rb_t *rb, rb_region_t *region);
static inline size_t rb_frame_to_byte_count(const rb_t *rb, size_t count);
static inline size_t rb_byte_to_frame_count(const rb_t *rb, size_t count);
static inline size_t rb_second_to_byte_count(double seconds, int sample_rate, int channel_count, int bytes_per_sample);
static inline int rb_try_exclusive_read(rb_t *rb);
static inline void rb_release_read(rb_t *rb);
static inline int rb_try_exclusive_write(rb_t *rb);
static inline void rb_release_write(rb_t *rb);
static inline void *buf_ptr(const rb_t *rb);
static inline void rb_debug(const rb_t *rb);
static inline void rb_debug_linearbar(const rb_t *rb);
static inline void rb_print_regions(const rb_t *rb);

/**
 * n/a
 */
//=============================================================================
static inline void rb_set_common_init_values(rb_t *rb)
{
	strncpy(rb->magic, RB_MAGIC, 8);
	rb->version=RB_VERSION;

	rb->write_index=0;
	rb->read_index=0;
	rb->last_was_write=0;
	rb->memory_locked=0;
	rb->no_more_input_data=0;

	rb->total_bytes_read=0;
	rb->total_bytes_write=0;
	rb->total_bytes_peek=0;
	rb->total_underflows=0;
	rb->total_overflows=0;

#ifndef RB_DISABLE_RW_MUTEX
	#ifndef RB_DISABLE_SHM
		pthread_mutexattr_init(&rb->mutex_attributes);
		pthread_mutexattr_setpshared(&rb->mutex_attributes, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&rb->read_lock, &rb->mutex_attributes);
		pthread_mutex_init(&rb->write_lock, &rb->mutex_attributes);
	#else
		pthread_mutex_init(&rb->read_lock, NULL);
		pthread_mutex_init(&rb->write_lock, NULL);
	#endif
#endif
}

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
	const char *a="anonymous";
	return rb_new_audio(size,a,0,1,1);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_named(size_t size, const char *name)
{
	return rb_new_audio(size,name,0,1,1);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_audio_seconds(double seconds, const char *name, int sample_rate, int channel_count, int bytes_per_sample)
{
	size_t size=rb_second_to_byte_count(seconds,sample_rate,channel_count,bytes_per_sample);
	return rb_new_audio(size,name,sample_rate,channel_count, bytes_per_sample);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_audio(size_t size, const char *name, int sample_rate, int channel_count, int bytes_per_sample)
{
#ifndef RB_DISABLE_SHM
	#ifdef RB_DEFAULT_USE_SHM
		return rb_new_shared_audio(size,name,sample_rate,channel_count,bytes_per_sample);
	#endif
#endif
	if(size<1) {return NULL;}

	rb_t *rb;

	//malloc space for rb_t struct and buffer
	rb=(rb_t*)malloc(sizeof(rb_t) + size); //
	if(rb==NULL) {return NULL;}

	//the attached buffer is in the same malloced space
	//right after rb_t (at offset sizeof(rb_t))
	rb_set_common_init_values(rb);
	rb->size=size;
	rb->in_shared_memory=0;
	rb->unlink_requested=0;
	rb->sample_rate=sample_rate;
	rb->channel_count=channel_count;
	rb->bytes_per_sample=bytes_per_sample;
	strncpy(rb->human_name, name, 255);
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
	const char *a="anonymous";
	return rb_new_shared_audio(size,a,0,1,1);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_shared_named(size_t size, const char *name)
{
	return rb_new_shared_audio(size,name,0,1,1);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_shared_audio_seconds(double seconds, const char *name, int sample_rate, int channel_count, int bytes_per_sample)
{
	size_t size=rb_second_to_byte_count(seconds,sample_rate,channel_count,bytes_per_sample);
	return rb_new_shared_audio(size,name,sample_rate,channel_count, bytes_per_sample);
}

/**
 * n/a
 */
//=============================================================================
static inline rb_t *rb_new_shared_audio(size_t size, const char *name, int sample_rate, int channel_count, int bytes_per_sample)
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
	if(r!=0)
	{	close(fd);
		shm_unlink(shm_handle);
		return NULL;
	}

	//void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	rb=(rb_t*)mmap(0, sizeof(rb_t) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if(rb==NULL || rb==MAP_FAILED)
	{
		shm_unlink(shm_handle);
		return NULL;
	}
//	fprintf(stderr,"rb address %lu\n",(unsigned long int)rb);
	memcpy(rb->shm_handle,shm_handle,37);
//	fprintf(stderr,"buffer address %lu\n",(unsigned long int)buf_ptr(rb));

	rb_set_common_init_values(rb);
	rb->size=size;
	rb->in_shared_memory=1;
	rb->unlink_requested=0;
	rb->sample_rate=sample_rate;
	rb->channel_count=channel_count;
	rb->bytes_per_sample=bytes_per_sample;
	strncpy(rb->human_name, name, 255);

//	rb_debug_linearbar(rb);

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
	//O_TRUNC | O_CREAT | 
	int fd=shm_open(shm_handle,O_RDWR, 0666);
	if(fd<0) {return NULL;}

//??
//	int r=ftruncate(fd,sizeof(rb_t));
//	if(r!=0) {return NULL;}

	//void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	rb=(rb_t*)mmap(0, sizeof(rb_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if(rb==NULL || rb==MAP_FAILED)
	{
		close(fd);
		shm_unlink(shm_handle);
		return NULL;
	}

	if(strncmp(rb->magic,RB_MAGIC,8))
	{
		fprintf(stderr,"MAGIC did not match! Was looking for '%s', found '%s'\n"
			,RB_MAGIC,rb->magic);
		close(fd);
		munmap(rb,sizeof(rb_t));
		shm_unlink(shm_handle);
		return NULL;
	}

	if(rb->version != RB_VERSION)
	{
		fprintf(stderr,"Version mismatch! Was looking for '%.3f', found '%.3f'\n"
			,RB_VERSION,rb->version);
		close(fd);
		munmap(rb,sizeof(rb_t));
		shm_unlink(shm_handle);
		return NULL;
	}

//	fprintf(stderr,"size %zu\n",rb->size);
	size_t size=rb->size;

	//unmap and remap fully (knowing size now)
	munmap(rb,sizeof(rb_t));
	rb=(rb_t*)mmap(0, sizeof(rb_t) + size , PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	if(rb==NULL || rb==MAP_FAILED)
	{
		shm_unlink(shm_handle);
		return NULL;
	}

//	fprintf(stderr,"rb address %lu\n",(unsigned long int)rb);
//	fprintf(stderr,"buffer address %lu\n",(unsigned long int)buf_ptr(rb));

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
	rb->unlink_requested=1;
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
		//shm_unlink(rb->shm_handle);
		char handle[255];
		strncpy(handle,rb->shm_handle,255);
		size_t size=rb->size;
		munmap(rb,sizeof(rb_t)+size);
		shm_unlink(handle);
		rb=NULL;
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
 * n/a
 */
//=============================================================================
static inline void rb_reset_stats(rb_t *rb)
{
	rb->total_bytes_read=0;
	rb->total_bytes_write=0;
	rb->total_bytes_peek=0;
	rb->total_underflows=0;
	rb->total_overflows=0;
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
	rb_reset_stats(rb);
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
 * n/a
 */
//=============================================================================
static inline size_t rb_can_read_frames(const rb_t *rb)
{
	return floor((double)rb_can_read(rb)/rb->channel_count/rb->bytes_per_sample);
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
 * n/a
 */
//=============================================================================
static inline size_t rb_can_write_frames(const rb_t *rb)
{
	return floor((double)rb_can_write(rb)/rb->channel_count/rb->bytes_per_sample);
}

/**
 * n/a
 */
//=============================================================================
static inline size_t rb_generic_read(rb_t *rb, char *destination, size_t count, int over)
{
	if(count==0) {return 0;}

	size_t can_read_count;
	size_t do_read_count;

	if(over)
	{
		can_read_count=rb_can_read(rb);
		//limit to whole buffer
		do_read_count=MIN(rb->size,count);
	}
	else
	{
		if(!(can_read_count=rb_can_read(rb))) {return 0;}
		do_read_count=count>can_read_count ? can_read_count : count;
	}

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

	//if write index was overpassed, move up to read index
	if(over && can_read_count<do_read_count)
	{
		rb->write_index=rb->read_index;
	}

	rb->last_was_write=0;
	rb->total_bytes_read+=do_read_count;
	if(do_read_count<count){rb->total_underflows++;}

	return do_read_count;
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
	return rb_generic_read(rb,destination,count,0);
}

/**
 * Read data from the ringbuffer and move the read index accordingly.
 * Opposed to rb_read() this function will read over the limit given 
 * by the current write index.
 * If the read goes beyond the write index the write index will be set 
 * equal to the resulting read index of this function. A writer will 
 * see the buffer having a write capacity equal to the buffer size.
 * 
 * Since rb_overread() could move the write index, it should be called
 * only if there is no ongoing rb_write operation.
 *
 * This is a copying data reader.
 *
 * The count of bytes effectively read can be less than the requested
 * count, which is limited to rb_size() bytes.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param destination a pointer to a buffer where data read from the
 * ringbuffer will be copied to.
 * @param count the number of bytes to read.
 *
 * @return the number of bytes read, which may range from 0 to rb_size().
 */
//=============================================================================
static inline size_t rb_overread(rb_t *rb, char *destination, size_t count)
{
	return rb_generic_read(rb,destination,count,1);
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
	rb->total_bytes_write+=do_write_count;
	if(do_write_count<count){rb->total_overflows++;}
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
static inline size_t rb_peek(rb_t *rb, char *destination, size_t count)
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
static inline size_t rb_peek_at(rb_t *rb, char *destination, size_t count, size_t offset)
{
	if(count==0) {return 0;}
	size_t can_read_count;
	//can not read more than offset, no chance to read from there
	if((can_read_count=rb_can_read(rb))<=offset)
	{
		rb->total_underflows++;
		return 0;
	}
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

	rb->total_bytes_peek+=do_read_count;
	if(do_read_count<count){rb->total_underflows++;}

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
static inline size_t rb_peek_byte(rb_t *rb, char *destination)
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
static inline size_t rb_peek_byte_at(rb_t *rb, char *destination, size_t offset)
{
	size_t can_read_count;
	if((can_read_count=rb_can_read(rb))<=offset)
	{
		rb->total_underflows++;
		return 0;
	}

	size_t tmp_read_index=rb->read_index+offset;

	if(rb->size<=tmp_read_index)
	{
		memcpy(destination, &(  ((char*)buf_ptr(rb))  [tmp_read_index-rb->size]),1);
	}
	else
	{
		memcpy(destination, &(  ((char*)buf_ptr(rb))  [tmp_read_index]),1);
	}

	rb->total_bytes_peek+=1;

	return 1;
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
 * n/a
 */
//=============================================================================
static inline size_t rb_deinterleave_items(rb_t *rb, char *destination
	,size_t item_count, size_t item_size, size_t initial_item_offset, size_t item_block_size)
{
/*
              offset
              |                       |                       |                        |
              0                       8                       16                       24 
can_read min one channel: initial_offset * item_size + (item_count -1 ) * item_size * block_size + item_size)
              |  |  |  |  |           |  |  |  |  |           |  |  |  |  |

                          |absolute min read (less than requested)        |request satisfied
                                                                                       |for all channels in block
*/

	size_t initial_bytepos=initial_item_offset * item_size;
	size_t concerned_block_size=(item_count - 1) * item_size * item_block_size + item_size;

	if(rb_can_read(rb) < initial_bytepos + concerned_block_size) {return 0;}

	char *destination_ptr=destination;

	size_t bytepos=0;
	for(bytepos=0;bytepos < initial_bytepos + concerned_block_size;bytepos+=item_size * item_block_size)
	{
//		fprintf(stderr,"offset + bytepos: %zu\n",initial_bytepos+bytepos);
		rb_peek_at(rb, destination_ptr,item_size,initial_bytepos+bytepos);
		destination_ptr+=item_size;
	}
	return item_count*item_size;
}

/**
 * n/a
 */
//=============================================================================
static inline size_t rb_deinterleave_audio(rb_t *rb, char *destination ,size_t frame_count, size_t frame_offset)
{
	return rb_deinterleave_items(rb, destination
		,frame_count, rb->bytes_per_sample, frame_offset, rb->channel_count);
}

/*
* n/a
*/
//=============================================================================
static inline size_t rb_generic_advance_read_index(rb_t *rb, size_t count, int over)
{
	if(count==0) {return 0;}
	size_t can_read_count;
	size_t do_advance_count;

	if(over)
	{
		can_read_count=rb_can_read(rb);
		//limit to whole buffer
		do_advance_count=MIN(rb->size,count);
	}
	else
	{
		if(!(can_read_count=rb_can_read(rb)))
		{
			rb->total_underflows++;
			return 0;
		}
		do_advance_count=count>can_read_count ? can_read_count : count;
	}
	size_t r=rb->read_index;
	size_t linear_end=r+do_advance_count;
	size_t tmp_read_index=linear_end>rb->size ? linear_end-rb->size : r+do_advance_count;

	rb->read_index=(tmp_read_index%=rb->size);

	//if write index was overpassed, move up to read index
	if(over && can_read_count<do_advance_count)
	{
		rb->write_index=rb->read_index;
	}

	rb->last_was_write=0;

	rb->total_bytes_read+=do_advance_count;
	if(do_advance_count<count){rb->total_underflows++;}

	return do_advance_count;
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
	return rb_generic_advance_read_index(rb,count,0);
}

/*
* n/a
*/
//=============================================================================
static inline size_t rb_overadvance_read_index(rb_t *rb, size_t count)
{
	return rb_generic_advance_read_index(rb,count,1);
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
	if(!(can_write_count=rb_can_write(rb)))
	{
		rb->total_overflows++;
		return 0;
	}

	size_t do_advance_count=count>can_write_count ? can_write_count : count;
	size_t w=rb->write_index;
	size_t linear_end=w+do_advance_count;
	size_t tmp_write_index=linear_end>rb->size ? linear_end-rb->size : w+do_advance_count;

	rb->write_index=(tmp_write_index%=rb->size);
	rb->last_was_write=1;

	rb->total_bytes_write+=do_advance_count;
	if(do_advance_count<count){rb->total_overflows++;}

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
 * n/a
 */
//=============================================================================
static inline size_t rb_frame_to_byte_count(const rb_t *rb, size_t count)
{
	return count * rb->channel_count * rb->bytes_per_sample;	
}

/**
 * n/a
 */
//=============================================================================
static inline size_t rb_byte_to_frame_count(const rb_t *rb, size_t count)
{
	return (size_t)(count/rb->channel_count/rb->bytes_per_sample);
}

/**
 * n/a
 */
//=============================================================================
static inline size_t rb_second_to_byte_count(double seconds, int sample_rate, int channel_count, int bytes_per_sample)
{
	size_t frames=ceil(seconds*sample_rate);
	return frames * channel_count * bytes_per_sample;
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
static inline void rb_debug(const rb_t *rb)
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
* Print out information about rinbuffer including a bar graph to indicate
* the buffer fill level.
*/
//=============================================================================
static inline void rb_debug_linearbar(const rb_t *rb)
{
	if(rb==NULL)
	{
		fprintf(stderr,"rb is NULL\n");
		return;
	}
	fprintf(stderr,"%s (v%.3f): %s\n",rb->shm_handle,rb->version,rb->human_name);
	if(rb->sample_rate>0)
	{
		fprintf(stderr,"audio: %3d channels @ %6d Hz, %2d bytes per sample, capacity %9.3f s\nmultichannel frames can read: %8zu can write: %8zu\n"
			,rb->channel_count
			,rb->sample_rate
			,rb->bytes_per_sample
			,(float)rb->size/rb->bytes_per_sample/rb->sample_rate/rb->channel_count
			,rb_can_read_frames(rb)
			,rb_can_write_frames(rb)
		);
	}

	fprintf(stderr,"r/w %.3f w %"PRId64" r %"PRId64" d %"PRId64" p %"PRId64" o %"PRId64" u %"PRId64" \n"
		,rb->total_bytes_write!=0 ?
			(float)rb->total_bytes_read/rb->total_bytes_write 
			: 0
		,rb->total_bytes_write
		,rb->total_bytes_read
		,rb->total_bytes_write>rb->total_bytes_read ?
			rb->total_bytes_write - rb->total_bytes_read 
			: rb->total_bytes_read - rb->total_bytes_write
		,rb->total_bytes_peek
		,rb->total_overflows
		,rb->total_underflows
	);

	int bar_ticks_count=45;
	size_t can_w=rb_can_write(rb);
	float fill_level;
	if(can_w==0)
	{
		fill_level=1;
	}
	else if(can_w==rb->size)
	{
		fill_level=0;
	}
	else
	{
		fill_level=1-(float)can_w/rb->size;
	}
	int bar_ticks_show=fill_level*bar_ticks_count;
	if(rb->sample_rate>0)
	{
		fprintf(stderr,"fill %.6f [%*.*s%s%*s] %9.3f s\n"
			,fill_level
			,bar_ticks_show
			,bar_ticks_show
			,bar_string
			,fill_level==0 ? "_" : (fill_level==1 ? "^" : ">")
			,(bar_ticks_count-bar_ticks_show)
			,""
			,(float)(rb->size-can_w)/rb->bytes_per_sample/rb->sample_rate/rb->channel_count
		);
	}
	else
	{
		fprintf(stderr,"fill %.6f [%*.*s%s%*s] %10zu\n"
			,fill_level
			,bar_ticks_show
			,bar_ticks_show
			,bar_string
			,fill_level==0 ? "_" : (fill_level==1 ? "^" : ">")
			,(bar_ticks_count-bar_ticks_show)
			,""
			,rb->size-can_w
		);
	}
}

/**
* Print out information about rinbuffer regions to stderr.
*/
//=============================================================================
static inline void rb_print_regions(const rb_t *rb)
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
static inline size_t rb_skip_all(rb_t *rb) {return rb_drop(rb);}

/**
* \brief This is an alias to rb_overadvance_read_index().
*/
static inline size_t rb_overskip(rb_t *rb, size_t count) {return rb_overadvance_read_index(rb,count);}

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
