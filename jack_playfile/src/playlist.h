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

#ifndef playlist_H_INC
#define playlist_H_INC

#include <fstream>
#include <string>
#include <vector>

//http://stackoverflow.com/questions/8436841/how-to-recursively-list-directories-in-c-on-linux
/* We want POSIX.1-2008 + XSI, i.e. SuSv4, features */
#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <unistd.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>

/* POSIX.1 says each process has at least 20 file descriptors.
 * Three of those belong to the standard streams.
 * Here, we use a conservative estimate of 15 available;
 * assuming we use at most two for other uses in this program,
 * we should never run into any problems.
 * Most trees are shallower than that, so it is efficient.
 * Deeper trees are traversed fine, just a bit slower.
 * (Linux allows typically hundreds to thousands of open files,
 *  so you'll probably never see any issues even if you used
 *  a much higher value, say a couple of hundred, but
 *  15 is a safe, reasonable value.)
*/
#ifndef USE_FDS
	#define USE_FDS 15
#endif

#include "sndin.h"

static char *playlist_file;
static int current_playlist_index=0;
static int no_more_files_to_play=0;

using std::ifstream;
using std::vector;
using std::string;
vector<string> files_to_play;

static int PL_DIRECTION_FORWARD=0;
static int PL_DIRECTION_BACKWARD=1;

static int pl_create(int argc, char *argv[], int from_playlist, int dump, int recurse);
static int pl_create_vector_from_args(int argc, char *argv[]);
static int pl_create_vector_from_file();
static int pl_open_check_file();
static void pl_set_next_index(int direction);
static int pl_check_file(const char *filepath);
static int pl_eventually_put_file_to_playlist(const char *filepath);

static void pl_handle_directory(const char *const dirpath);
static int pl_directory_scanner_callback(const char *filepath, const struct stat *info, const int typeflag, struct FTW *pathinfo);

///
static int pl_dump=0;
static int pl_recurse=0;
static int pl_test_number=0;

//wrapper to create playlist (vector of file uri strings) from file or from argv
//=============================================================================
static int pl_create(int argc, char *argv[], int from_playlist, int dump, int recurse)
{
	pl_dump=dump;
	pl_recurse=recurse;
	if(!from_playlist)
	{
		//remaining non optional parameters must be at least one file
		if(argc-optind<1)
		{
			fprintf(stderr, "Wrong arguments, see --help.\n");
			return 0;
		}

		if(!pl_create_vector_from_args(argc,argv))
		{
			fprintf(stderr,"/!\\ could not create playlist\n");
			return 0;
		}
	}
	else
	{
		if(argc-optind>=1)
		{
			fprintf(stderr,"/!\\ can't mix -F playlist with args\n");
			return 0;
		}

		if(!pl_create_vector_from_file())
		{
			fprintf(stderr,"/!\\ could not create playlist\n");
			return 0;
		}
	}
	return 1;
}

//=============================================================================
static int pl_eventually_put_file_to_playlist(const char *filepath)
{
	fprintf(stderr,"\r%s\rparsing arguments file # %d ",clear_to_eol_seq,pl_test_number);
	if(pl_check_file(filepath))
	{
		files_to_play.push_back(filepath);
		if(pl_dump)
		{
			fprintf(stdout,"%s\n",filepath);
			fflush(stdout);
		}
		pl_test_number++;
		return 1;
	}
	pl_test_number++;
	return 0;
}

//=============================================================================
static int pl_create_vector_from_args(int argc, char *argv[])
{
	fprintf(stderr,"%s",turn_off_cursor_seq);
	pl_test_number=1;

	while(argc-optind>0)
	{
		if(!pl_eventually_put_file_to_playlist(argv[optind]))
		{
			if(pl_recurse)
			{
				pl_test_number--;
				pl_handle_directory(argv[optind]);
			}
		}
		optind++;
	}

	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr,"%s",turn_on_cursor_seq);
	return 1;
}

//=============================================================================
static int pl_create_vector_from_file()
{
	ifstream ifs(playlist_file);
	string line;

	fprintf(stderr,"%s",turn_off_cursor_seq);
	pl_test_number=1;

	while ( std::getline(ifs, line) )
	{
		if (line.empty())
		{
			continue;
		}
		pl_eventually_put_file_to_playlist(line.c_str());
	}

	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr,"%s",turn_on_cursor_seq);
	ifs.close();
	return 1;
}

//=============================================================================
static int pl_open_check_file()
{
	if(current_playlist_index<0)
	{
		current_playlist_index=0;
	}

	if(current_playlist_index>=files_to_play.size())
	{
		no_more_files_to_play=1;
		return 0;
	}

	if(pl_check_file(files_to_play[current_playlist_index].c_str()))
	{
		return 1;
	}
	else
	{
		//remove bogus file (must have changed or was deleted since create_playlist_vector())
		files_to_play.erase(files_to_play.begin() + current_playlist_index);
		//recurse
		//return pl_open_check_file();
		return 0;
	}
}

//increment or decrement position in playlist depending on requested direction
//=============================================================================
static void pl_set_next_index(int direction)
{
	if(direction)
	{
		current_playlist_index--;
	}
	else
	{
		current_playlist_index++;
	}
}

//test quietly if a file can be opened as playable audio file
//=============================================================================
static int pl_check_file(const char *filepath)
{
	//sin_open, sf_info_generic in sndin.h
	if(!sin_open(filepath,1))
	{
		return 0;
	}

	sin_close();
	return 1;
}

//=============================================================================
static int pl_directory_scanner_callback(
	const char *filepath, const struct stat *info, const int typeflag, struct FTW *pathinfo)
{
	if (typeflag == FTW_SL)
	{
		pl_eventually_put_file_to_playlist(filepath);
	} 
//	else if (typeflag == FTW_SLN) {fprintf(stderr," %s (dangling symlink)\n", filepath);}
	else if (typeflag == FTW_F)
	{
//		fprintf(stderr," %s\n", filepath);
                pl_eventually_put_file_to_playlist(filepath);
	}
/*
	else if (typeflag == FTW_D || typeflag == FTW_DP) {fprintf(stderr," %s/\n", filepath);}
	else if (typeflag == FTW_DNR) {fprintf(stderr," %s/ (unreadable)\n", filepath);}
	else {fprintf(stderr," %s (unknown)\n", filepath);}
*/
	return 0;
}

//=============================================================================
static void pl_handle_directory(const char *const dirpath)
{
	/* Invalid directory path? */
	if (dirpath == NULL || *dirpath == '\0') {return;}
	int result = nftw(dirpath, pl_directory_scanner_callback, USE_FDS, FTW_PHYS);
}

#endif
//EOF
