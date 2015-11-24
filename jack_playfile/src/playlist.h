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

static char *playlist_file;
static int current_playlist_index=0;
static int no_more_files_to_play=0;

using std::ifstream;
using std::vector;
using std::string;
vector<string> files_to_play;

static int create_playlist(int argc, char *argv[], int from_playlist, int dump);
static int create_playlist_vector_from_args(int argc, char *argv[], int dump);
static int create_playlist_vector_from_file(int dump);
static int open_init_file_from_playlist();
static void set_playlist_index(int prev);
static int check_file(const char *f);

//wrapper to create playlist (vector of file uri strings) from file or from argv
//=============================================================================
static int create_playlist(int argc, char *argv[], int from_playlist, int dump)
{
	if(!from_playlist)
	{
		//remaining non optional parameters must be at least one file
		if(argc-optind<1)
		{
			//print_header();
			fprintf(stderr, "Wrong arguments, see --help.\n");
			return 0;
		}

		if(!create_playlist_vector_from_args(argc,argv,dump))
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

		if(!create_playlist_vector_from_file(dump))
		{
			fprintf(stderr,"/!\\ could not create playlist\n");
			return 0;
		}
	}
	return 1;
}

//=============================================================================
static int create_playlist_vector_from_args(int argc, char *argv[], int dump)
{
	fprintf(stderr,"%s",turn_off_cursor_seq);
	int test_no=1;
	while(argc-optind>0)
	{
		fprintf(stderr,"\r%s\rparsing arguments file # %d",clear_to_eol_seq,test_no);
		if(check_file(argv[optind]))
		{
			if(!dump)
			{
				files_to_play.push_back(argv[optind]);
			}
			else
			{
				fprintf(stdout,"%s\n",argv[optind]);
				fflush(stdout);
			}
		}
		optind++;
		test_no++;
	}

	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr,"%s",turn_on_cursor_seq);
	return 1;
}

//=============================================================================
static int create_playlist_vector_from_file(int dump)
{
	ifstream ifs(playlist_file);
	string line;

	fprintf(stderr,"%s",turn_off_cursor_seq);
	int test_no=1;
	while ( std::getline(ifs, line) )
	{
		if (line.empty())
		{
			continue;
		}

		fprintf(stderr,"\r%s\rparsing playlist file # %d ",clear_to_eol_seq,test_no);

		if(check_file(line.c_str()))
		{
			if(!dump)
			{
				files_to_play.push_back(line);
			}
			else
			{
				fprintf(stdout,"%s\n",line.c_str());
				fflush(stdout);
			}
		}

		test_no++;
	}

	fprintf(stderr,"\r%s\r",clear_to_eol_seq);
	fprintf(stderr,"%s",turn_on_cursor_seq);
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
	memset (&sf_info_generic, 0, sizeof (sf_info_generic)) ;

	if(!(sin_open(f,&sf_info_generic,1)))
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
