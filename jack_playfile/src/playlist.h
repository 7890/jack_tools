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

#ifndef playlist_H_INC
#define playlist_H_INC

#include <fstream>
#include <string>
#include <vector>

static int read_from_playlist=0;
static char *playlist_file;
static int current_playlist_index=0;
static int no_more_files_to_play=0;

using std::ifstream;
using std::vector;
using std::string;
vector<string> files_to_play;

static int create_playlist(int argc, char *argv[]);
static int create_playlist_vector_from_args(int argc, char *argv[]);
static int create_playlist_vector_from_file();
static int open_init_file_from_playlist();
static void set_playlist_index(int prev);
static int check_file(const char *f);

//wrapper to create playlist (vector of file uri strings) from file or from argv
//=============================================================================
static int create_playlist(int argc, char *argv[])
{
	if(!read_from_playlist)
	{
		//remaining non optional parameters must be at least one file
		if(argc-optind<1)
		{
			//print_header();
			fprintf(stderr, "Wrong arguments, see --help.\n");
			return 0;
		}

		if(!create_playlist_vector_from_args(argc,argv))
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

		if(!create_playlist_vector_from_file())
		{
			fprintf(stderr,"/!\\ could not create playlist\n");
			return 0;
		}
	}
	return 1;
}

//=============================================================================
static int create_playlist_vector_from_args(int argc, char *argv[])
{
	fprintf(stderr,"parsing arguments");

	while(argc-optind>0)
	{
		if(is_verbose)
		{
			fprintf(stderr,".");
		}
		if(check_file(argv[optind]))
		{
			files_to_play.push_back(argv[optind]);
		}
		optind++;
	}

	fprintf(stderr,"\n%d usable audio files in playlist\n",(int)files_to_play.size());
	return 1;
}

//=============================================================================
static int create_playlist_vector_from_file()
{
	ifstream ifs(playlist_file);
	string line;

	fprintf(stderr,"parsing playlist");

	while ( std::getline(ifs, line) )
	{
		if (line.empty())
		{
			continue;
		}

		if(is_verbose)
		{
			fprintf(stderr,".");
		}
		if(check_file(line.c_str()))
		{
			files_to_play.push_back(line);
		}
	}

	fprintf(stderr,"\n%d usable audio files in playlist\n",(int)files_to_play.size());

	ifs.close();
	return 1;
}

//=============================================================================
static int open_init_file_from_playlist()
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

	if(open_init_file(files_to_play[current_playlist_index].c_str()))
	{
		return 1;
	}
	else
	{
		//remove bogus file (must have changed or was deleted since create_playlist_vector())
		files_to_play.erase(files_to_play.begin() + current_playlist_index);
		//recurse
		return open_init_file_from_playlist();
	}
}

//increment or decrement position in playlist depending on requested direction
//=============================================================================
static void set_playlist_index(int prev)
{
	if(prev)
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
static int check_file(const char *f)
{
	filename=f;
	memset (&sf_info_generic, 0, sizeof (sf_info_generic)) ;

	if(!(sin_open(filename,&sf_info_generic,1)))
	{
		return 0;
	}

	if(sf_info_generic.frames<1)
	{
		return 0;
	}

	sin_close();
	return 1;
}

#endif
//EOF
