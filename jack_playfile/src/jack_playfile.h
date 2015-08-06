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

#ifndef JACK_PLAYFILE_H_INC
#define JACK_PLAYFILE_H_INC

static const float version=0.8;

//================================================================
int main(int argc, char *argv[]);

static int disk_read_frames();
static void *disk_thread_func(void *arg);
static void setup_disk_thread();
static void req_buffer_from_disk_thread();

static void set_seconds_from_exponent();
static void set_frames_from_exponent();
static void increment_seek_step_size();
static void decrement_seek_step_size();

static void seek_frames_absolute(int64_t frames_abs);
static void seek_frames(int64_t frames_rel);

static void deinterleave();

static void print_stats();

static void signal_handler(int sig);

#endif
//EOF
