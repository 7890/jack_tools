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

#ifndef MANPAGE_H_INC
#define MANPAGE_H_INC
/*
wrapper to data file created from manual page with xxd

zcat doc/jack_playfile.1.gz | groff -Tascii -man - > jack_playfile_man_dump
xxd -i jack_playfile_man_dump | sed 's/};/,0x00};\/\//g' | sed 's/;$/+1;/g' > build/manpage_data.h
*/
//http://stackoverflow.com/questions/410980/include-a-text-file-in-a-c-program-as-a-char

#ifdef STATIC_BUILD
	#include "../build/manpage.data.h" //the fixed path is not nice (doesn't respect Makefile variables)
	#include "../build/build_info.data.h"
#endif

#endif
//EOF
