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

/*
format specialities:

FLAC: maximum of 8 channels

	in opusfile-0.6/src/internal.h:
	//The maximum number of channels permitted by the format.
	#define FLAC__MAX_CHANNELS (8u)

Opus: maximum of 8 channels

	in opusfile-0.6/src/internal.h:
	//The maximum channel count for any mapping we'll actually decode
	# define OP_NCHANNELS_MAX (8)

	from opusfile.h
	Opus files can contain anywhere from 1 to 255 channels of audio.
	Sampling rates 8000, 12000, 16000, 24000, 48000

	The channel mappings for up to *8* channels are the same as the
	<a href="http://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9">Vorbis mappings</a>.
	Although the <tt>libopusfile</tt> ABI provides support for the theoretical
	maximum number of channels, the current implementation (libopusfile)

	***does not support files with more than 8 channels***

	 as they do not have well-defined channel mappings.

	(note: iiuc opusfile supports 48000 only)

MP3:

	https://en.wikipedia.org/wiki/MP3
	Several bit rates are specified in the MPEG-1 Audio Layer III standard: 
	32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256 and 320 kbit/s, 
	with available sampling frequencies of 32, 44.1 and 48 kHz. 
	MPEG-2 Audio Layer III allows bit rates of 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 kbit/s 
	with sampling frequencies of 16, 22.05 and 24 kHz. 
	MPEG-2.5 Audio Layer III is restricted to bit rates of 8, 16, 24, 32, 40, 48, 56 and 64 kbit/s 
	with sampling frequencies of 8, 11.025, and 12 kHz.

	(note: iiuc limited to two channels)

*/

#ifndef SNDIN_H_INC
#define SNDIN_H_INC

#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#include <sndfile.h>
#include <opusfile.h>
#include <vorbis/vorbisfile.h>
#include <mpg123.h>

//g++ -o test test.c `pkg-config --libs --cflags opusfile sndfile libmpg123 vorbisfile`

typedef struct
{
	sf_count_t frames; //Used to be called samples.
	int sample_rate;
	int channels;
	int format;
} SF_INFO_GENERIC;

#define SF_FORMAT_OPUS 0x900000
#define SF_FORMAT_MP3 0x910000

//handle to currently playing file
static SNDFILE *soundfile=NULL;

//holding basic audio file properties
static SF_INFO sf_info_sndfile;

//unified with other formats
static SF_INFO_GENERIC sf_info_generic;

//try mpg123 reader
static mpg123_handle *soundfile_123=NULL;

//yet another reader
static OggOpusFile *soundfile_opus;

//..and another (for faster seeking)
static OggVorbis_File soundfile_vorbis;

//oggvorbis takes a regular file
FILE *ogg_file;

//multichannel float buffer for ov_read_float
static float **ogg_buffer;

static int is_mpg123=0;
static int is_opus=0;
static int is_ogg=0;
static int is_flac=0;

//pseudo "lock"
int closing_file_in_progress=0;

static float *tmp_buffer=new float[10000000]; ///

static int sin_open(const char *fileuri, int quiet);

static int sin_check_if_mp3_file(const char *filename);
static sf_count_t sin_seek(sf_count_t offset, int whence);
static void sin_close();

static int64_t sin_read_frames_from_file_to_buffer(float *buffer,uint64_t frame_count,int channel_offset,int channel_count);

static double sin_frames_to_seconds(sf_count_t frames, int sample_rate);
static double sin_get_seconds(SF_INFO_GENERIC *sf_info);
static const char * sin_format_duration_str(double seconds);
static const char * sin_generate_duration_str(SF_INFO_GENERIC *sf_info);

static int sin_is_flac(SF_INFO_GENERIC *sf_info);
static int sin_is_ogg(SF_INFO_GENERIC *sf_info);

static int sin_file_info(SF_INFO_GENERIC sf_info, int print);

//return 0 on error, 1 on success
//=============================================================================
static int sin_open(const char *fileuri, int quiet)
{
	is_opus=0;
	is_ogg=0;
	is_mpg123=0;
	is_flac=0;

	struct stat st;
	stat(fileuri, &st);
/*
S_IFSOCK   0140000   socket
S_IFLNK    0120000   symbolic link
S_IFREG    0100000   regular file
S_IFBLK    0060000   block device
S_IFDIR    0040000   directory
S_IFCHR    0020000   character device
S_IFIFO    0010000   FIFO
*/
	//ignore if not a regular file or a symbolic link
	if((st.st_mode & S_IFMT) != S_IFREG
#ifndef WIN32
		&& (st.st_mode & S_IFMT) != S_IFLNK
#endif
	)
	{
		return 0;
	}

	memset (&sf_info_generic, 0, sizeof (SF_INFO_GENERIC));
	memset (&sf_info_sndfile, 0, sizeof (SF_INFO));
	/*
	typedef struct
	{ sf_count_t  frames ; //Used to be called samples.
	int	sample_rate ;
	int	channels ;
	int	format ;
	int	sections ;
	int	seekable ;
	} SF_INFO ;
	*/

	//init soundfile
	soundfile=sf_open(fileuri,SFM_READ,&sf_info_sndfile);
	if(soundfile!=NULL)
	{
		sf_info_generic.frames=sf_info_sndfile.frames;
		sf_info_generic.sample_rate=sf_info_sndfile.samplerate;
		sf_info_generic.channels=sf_info_sndfile.channels;
		sf_info_generic.format=sf_info_sndfile.format;

		//use vorbisfile to decode ogg
		if(sin_is_ogg(&sf_info_generic))
		{
			sf_close(soundfile);

			ogg_file = fopen(fileuri,"r");
			ov_open(ogg_file,&soundfile_vorbis,NULL,0);
			is_ogg=1;
		}
		else if(sin_is_flac(&sf_info_generic))
		{
			is_flac=1;
		}
	}
	else
	{
		//try opus
		int ret;
		soundfile_opus=op_open_file(fileuri,&ret);

		if(!ret && soundfile_opus!=NULL)
		{
			is_opus=1;

			//seek to end, get frame count
//			fprintf(stderr,"/!\\ reducing frame count by 1\n");
			sf_info_generic.frames=op_pcm_total(soundfile_opus,-1) - 1; //-1 not nice
			//the libopusfile API always decodes files to 48 kHz.
			sf_info_generic.sample_rate=48000;
			sf_info_generic.channels=op_channel_count(soundfile_opus,0);
			sf_info_generic.format=SF_FORMAT_OPUS | SF_FORMAT_FLOAT;
		}
		else
		{
			//try mp3
			if(!sin_check_if_mp3_file(fileuri))
			{
				return 0;
			}

			mpg123_init();

			soundfile_123=mpg123_new(NULL, NULL);

			mpg123_format_none(soundfile_123);
			int format=mpg123_format(soundfile_123
				,48000
				,MPG123_STEREO
				,MPG123_ENC_FLOAT_32
			);

			//suppress error messages
			if(quiet)
			{
				mpg123_param(soundfile_123, MPG123_FLAGS, MPG123_QUIET, 0);
			}
			int ret = mpg123_open(soundfile_123, fileuri);
			if(ret == MPG123_OK)
			{
				long rate=0;
				int channels=0;
				int format=0;
				mpg123_getformat(soundfile_123, &rate, &channels, &format);

				///problems with some files (?)
				//struct mpg123_frameinfo mp3info;
				//mpg123_info(soundfile_123, &mp3info);

				///if(format==0 || 
				if(mpg123_length(soundfile_123)<=0)
				{
					mpg123_close(soundfile_123);
					mpg123_delete(soundfile_123);
					if(!quiet)
					{
						fprintf (stderr, "/!\\ cannot open file \"%s\"\n", fileuri);
					}
					return 0;
				}

/*
				fprintf(stderr, "mp3 sampling rate %lu bitrate %d channels %d format %d\n"
					,rate
					,mp3info.bitrate
					,channels
					,format);
*/

				is_mpg123=1;

				//seek to end, get frame count
				//sf_info_generic.frames=mpg123_seek(soundfile_123,0,SEEK_END);
				//this is better
				sf_info_generic.frames=mpg123_length(soundfile_123);
				sf_info_generic.sample_rate=48000;
				sf_info_generic.channels=2;
				sf_info_generic.format=SF_FORMAT_MP3 | SF_FORMAT_FLOAT;
			}
			else 
			{
				mpg123_close(soundfile_123);
				mpg123_delete(soundfile_123);
				if(!quiet)
				{
					fprintf (stderr, "/!\\ cannot open file \"%s\"\n", fileuri);
				}
				return 0;
			}
		}//end try mpg123
	}//end try opus

	if(sf_info_generic.frames<1)
	{
		fprintf(stderr,"/!\\ file has zero frames, nothing to play!\n");
		return 0;
	}
	if(sf_info_generic.sample_rate<1)
	{
		fprintf(stderr,"/!\\ file has invalid sample rate, nothing to play!\n");
		return 0;
	}
	if(sf_info_generic.channels<1)
	{
		fprintf(stderr,"/!\\ file has no channels, nothing to play!\n");
		return 0;
	}

	//matching format found
	return 1;
}//end sin_open

//rough test if file could be of type mp3
//return 0 on error, 1 on success
//=============================================================================
static int sin_check_if_mp3_file(const char *filename)
{
	FILE *f=NULL;
	f=fopen(filename, "rb");
	if(f==NULL)
	{
		return 0;
	}

/*
http://git.cgsecurity.org/cgit/testdisk/tree/src/file_mp3.c
static const unsigned char mpeg1_L3_header1[2]= {0xFF, 0xFA};
static const unsigned char mpeg1_L3_header2[2]= {0xFF, 0xFB};
static const unsigned char mpeg2_L3_header1[2]= {0xFF, 0xF2};
static const unsigned char mpeg2_L3_header2[2]= {0xFF, 0xF3};
static const unsigned char mpeg25_L3_header1[2]={0xFF, 0xE2};
static const unsigned char mpeg25_L3_header2[2]={0xFF, 0xE3};
*/

	unsigned char bytes_pattern_with_id3[3]={0x49,0x44,0x33}; //ID3
	unsigned char bytes_header[3];

	size_t size=0;
	unsigned char c='\0';
	//find first non-zero byte in the first 1000 bytes
	for(int i=0;i<1000;i++)
	{
		size=fread(&c, 1, 1, f);
//		fprintf(stderr,"%d %zu %X\n",i,size,c);
		if(c!=0)
		{
//			fprintf(stderr,"first non-zero byte %d\n",i);
			//seek one back to read it again
			fseek(f,-1,SEEK_CUR);
//			fprintf(stderr,"at %zu\n",ftell(f));
			break;
		}
	}

	if(c==0)
	{
		fclose(f);
		return 0;
	}
	size=fread(bytes_header, 1, 3, f);
//	fprintf(stderr,"\n%X %X %X\n",bytes_header[0],bytes_header[1],bytes_header[2]);

	if(size<3)
	{
		fclose(f);
		return 0;
	}

	if(bytes_header[0] == bytes_pattern_with_id3[0]
		&& bytes_header[1] == bytes_pattern_with_id3[1]
		&& bytes_header[2] == bytes_pattern_with_id3[2]
	)
	{
		fclose(f);
		return 1;
	}
	else if(bytes_header[0] == 0xFF)
	{
		if(bytes_header[1] == 0xFA
			|| bytes_header[1] == 0xFA
			|| bytes_header[1] == 0xFB
			|| bytes_header[1] == 0xF2
			|| bytes_header[1] == 0xF3
			|| bytes_header[1] == 0xE2
			|| bytes_header[1] == 0xE3
		)
		{
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

//=============================================================================
static sf_count_t sin_seek(sf_count_t offset, int whence)
{
	if(is_mpg123)
	{
		if(whence==SEEK_CUR && offset==0)
		{
			return mpg123_tell(soundfile_123);
		}
		else
		{
			//returns the resulting offset >= 0 or error/message code
			return mpg123_seek(soundfile_123,offset,whence);
		}
	}
	else if(is_opus)
	{
		if(whence==SEEK_SET)
		{
			//op_pcm_seek returns 0 on success, or a negative value on error.
			op_pcm_seek(soundfile_opus,offset);
			return (sf_count_t)op_pcm_tell(soundfile_opus);
		}
		else if(whence==SEEK_CUR)
		{
			ogg_int64_t pos=op_pcm_tell(soundfile_opus);

			if(offset==0)
			{
				//no need to seek
				return pos;
			}
			else
			{
				//make relative seek absolute
				pos+=offset;
				return op_pcm_seek(soundfile_opus,pos);
			}
		}
	}
	else if(is_ogg)
	{
		if(whence==SEEK_SET)
		{
			//returns 0 for success
			ov_pcm_seek(&soundfile_vorbis,offset);
			return (sf_count_t)ov_pcm_tell(&soundfile_vorbis);
		}
		else if(whence==SEEK_CUR)
		{
			ogg_int64_t pos=ov_pcm_tell(&soundfile_vorbis);

			if(offset==0)
			{
				//no need to seek
				return pos;
			}
			else
			{
				//make relative seek absolute
				pos+=offset;
				return ov_pcm_seek(&soundfile_vorbis,pos);
			}
		}
	}
	else //sndfile
	{
		//sf_seek will return the offset in (multichannel) frames from the start of the audio data or -1 if an error occured
		return sf_seek(soundfile,offset,whence);
	}
}//end sf_seek_()

//=============================================================================
static void sin_close()
{
	closing_file_in_progress=1;
	if(is_mpg123)
	{
		if(soundfile_123!=NULL)
		{
			mpg123_close(soundfile_123);
			mpg123_delete(soundfile_123);
			soundfile_123=NULL;
		}
	}
	else if(is_opus)
	{
		if(soundfile_opus!=NULL)
		{
			///this sometimes creates ~double free corruption (?)
			op_free(soundfile_opus);
			soundfile_opus=NULL;
		}
	}
	else if(is_ogg)
	{
		if(ogg_file!=NULL)
		{
			ov_clear(&soundfile_vorbis);
			ogg_file=NULL;
		}
	}
	else //sndfile
	{
		if(soundfile!=NULL)
		{
			sf_close(soundfile);
			soundfile=NULL;
		}
	}
	closing_file_in_progress=0;
}//end sf_close()

//==============================================================================
static int64_t sin_read_frames_from_file_to_buffer(float *buffer
	,uint64_t frame_count
	,int channel_offset
	,int channel_count
)
{
	int64_t frames_to_go=frame_count;
	int64_t could_read_frame_count=0;

	int file_eof=0;

	int ogg_buffer_offset=0;
	int current_section;

	float *tmp_buffer_opus=tmp_buffer;

//	fprintf(stderr,"\nread_frames_from_file_to_buffer() called\n");

	while(frames_to_go>0 && !file_eof)
	{
//		fprintf(stderr,"\nto go %"PRId64"\n",frames_to_go);

		//------------------------------------ sndfile
		if(!is_mpg123 && !is_opus && !is_ogg)
		{
			could_read_frame_count=sf_readf_float(soundfile,(float*)tmp_buffer,frames_to_go);

			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);
				file_eof=1;
				break;
			}
			else
			{
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				//unlike opus or ogg vorbis, sndfile will return the requested number of bytes if not at end of file (partial).

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end sndfile
		//------------------------------------ opus
		else if(is_opus)
		{
/*
take care with channel maps!
default opusenc applies this matrix:
  {0},              //1.0 mono
  {0,1},            //2.0 stereo
  {0,2,1},          //3.0 channel ('wide') stereo
  {0,1,2,3},        //4.0 discrete quadraphonic
  {0,2,1,3,4},      //5.0 surround
  {0,2,1,4,5,3},    //5.1 surround
  {0,2,1,5,6,4,3},  //6.1 surround
  {0,2,1,6,7,4,5,3} /7.1 surround (classic theater 8-track)

-> playing 8_8_8.opus tells channels "1,3,2,7,8,5,6,4" (one-based) unless another map is chosen at encode time

Mapping (8 bits, 0=single stream (mono/stereo) 1=Vorbis mapping,
2..254: reserved, 255: multistream with no mapping)
*/

			//read multichannel as normal
			could_read_frame_count=op_read_float(soundfile_opus
				,(float*)tmp_buffer_opus
				,frames_to_go*sf_info_generic.channels
				,NULL);

			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);
				file_eof=1;
				break;
			}
			else 
			{
				frames_to_go-=could_read_frame_count;

				//set offset in buffer for next cycle
				tmp_buffer_opus+=could_read_frame_count*sf_info_generic.channels;
//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end opus
		//------------------------------------ ogg
		else if(is_ogg)
		{
			/*
			//https://xiph.org/vorbis/doc/vorbisfile/example.html
			//http://lists.xiph.org/pipermail/vorbis-dev/2002-January/005500.html

			long ov_read_float(OggVorbis_File *vf, float ***pcm_channels, int samples, int *bitstream);
			float **pcm; samples_read = ov_read_float(&vf, &pcm, 1024, &current_section)

			**pcm is a multichannel float vector. In stereo, for example, pcm[0] is left, and pcm[1] is right. 
			samples is the size of each channel.

			As you might expect, buffer[0][0] will be the first sample in the
			left channel, and buffer[1][0] will be the first sample in the right channel.

			Also make sure that you don't free() buffer anywhere since you don't own it.
			*/

			could_read_frame_count=ov_read_float(&soundfile_vorbis
				,&ogg_buffer
				,frames_to_go
				,&current_section);

			//interleave
			//(if no resampling is involved, we could directly put to rb_deinterleaved..)
			for(int i=0;i<could_read_frame_count;i++)
			{
				int buffer_index=i*sf_info_generic.channels;

				for(int k=0;k<sf_info_generic.channels;k++)
				{
					tmp_buffer[buffer_index+k+ogg_buffer_offset]=ogg_buffer[k][i];

//					fprintf(stderr,"buffer[%d] ogg_pcm[%d][%d]\n",buffer_index+k+ogg_buffer_offset,k,i);
					/*
					buffer: interleaved samples
					ogg_pcm [channel] [sample]
					...
					buffer[250] ogg_pcm[0][125]
					buffer[251] ogg_pcm[1][125]
					buffer[252] ogg_pcm[0][126]
					buffer[253] ogg_pcm[1][126]
					buffer[254] ogg_pcm[0][127]
					buffer[255] ogg_pcm[1][127]
					*/
				}
			}
			if(could_read_frame_count<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",could_read_frame_count);
				file_eof=1;
				break;
			}
			else
			{
				frames_to_go-=could_read_frame_count;
				//set offset in buffer for next cycle
				ogg_buffer_offset+=could_read_frame_count*sf_info_generic.channels;
//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end ogg
		//------------------------------------ mpg123
		else if(is_mpg123)
		{
			size_t fill=0;
			mpg123_read(soundfile_123,(unsigned char*)tmp_buffer
				,frames_to_go*sf_info_generic.channels*sizeof(float), &fill);

			if(fill<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",fill);
				file_eof=1;
				break;
			}
			else
			{
				could_read_frame_count=fill/(sf_info_generic.channels*sizeof(float));
				frames_to_go-=could_read_frame_count;
//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end mpg123
	}//end while(frames_to_go>0 && !file_eof)

	/*
	interleaved buffer

	|ch1|ch2|ch3|ch4|ch5|

	    |offset 1
	            |count 2

	    |---|---| copy
	*/

	//filter out unwanted channels, only copy requested
	if(channel_count>0)
	{
		int bindex=0;
		for(int i=0;i<(frame_count-frames_to_go);i++)
		{
			for(int k=0;k<sf_info_generic.channels;k++)
			{
				if(k>=channel_offset && k<channel_offset+channel_count)
				{
					buffer[bindex]=tmp_buffer[i*sf_info_generic.channels+k];
					bindex++;
//					fprintf(stderr,"+ %d %d\n",k,sf_info_generic.channels);
				}
				else
				{
//					fprintf(stderr,"- %d %d \n",k,sf_info_generic.channels);
				}
			}
		}
	}//end if(channel_count>=0)

	//in case of a normal, full buffer read, this will be equal to count
	return frame_count-frames_to_go;
}//end read_frames_from_file_to_buffer

//=============================================================================
static double sin_frames_to_seconds(sf_count_t frames, int sample_rate)
{
	double seconds;
	if (frames==0)
	{
		return 0;
	}
	seconds = (1.0 * frames) / sample_rate;
	return seconds;
}

//=============================================================================
static double sin_get_seconds(SF_INFO_GENERIC *sf_info)
{
	double seconds;
	if (sf_info->sample_rate < 1)
	{
		return 0;
	}
	if (sf_info->frames / sf_info->sample_rate > 0x7FFFFFFF)
	{
		return -1;
	}
	seconds = (1.0 * sf_info->frames) / sf_info->sample_rate;
	return seconds;
}

//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
//=============================================================================
static const char * sin_format_duration_str(double seconds)
{
	static char str [128] ;
	int hrs, min ;
	double sec ;
	memset (str, 0, sizeof (str)) ;
	hrs = (int) (seconds / 3600.0) ;
	min = (int) ((seconds - (hrs * 3600.0)) / 60.0) ;
	sec = seconds - (hrs * 3600.0) - (min * 60.0) ;
	snprintf (str, sizeof (str) - 1, "%02d:%02d:%06.3f", hrs, min, sec) ;
	return str ;
}

//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
//=============================================================================
static const char * sin_generate_duration_str(SF_INFO_GENERIC *sf_info)
{
	return(
		sin_format_duration_str(
			sin_get_seconds(sf_info)
		)
	);
}

//=============================================================================
static int sin_is_flac(SF_INFO_GENERIC *sf_info)
{
	if( (sf_info->format & SF_FORMAT_TYPEMASK)==SF_FORMAT_FLAC )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

//=============================================================================
static int sin_is_ogg(SF_INFO_GENERIC *sf_info)
{
	if( (sf_info->format & SF_FORMAT_TYPEMASK)==SF_FORMAT_OGG )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

//=============================================================================
static int sin_file_info(SF_INFO_GENERIC sf_info, int print)
{
	/*
	SF_FORMAT_SUBMASK  = 0x0000FFFF
	SF_FORMAT_TYPEMASK = 0x0FFF0000
	*/
	int bytes=0;

	char *format_string;

	switch (sf_info.format & SF_FORMAT_TYPEMASK)
	{
	//http://www.mega-nerd.com/libsndfile/api.html
	//major formats
	case SF_FORMAT_WAV	:format_string="Microsoft WAV format (little endian)"; break;
	case SF_FORMAT_AIFF	:format_string="Apple/SGI AIFF format (big endian)"; break;
	case SF_FORMAT_AU	:format_string="Sun/NeXT AU format (big endian)"; break;
	case SF_FORMAT_RAW	:format_string="RAW PCM data"; break;
	case SF_FORMAT_PAF	:format_string="Ensoniq PARIS file format"; break;
	case SF_FORMAT_SVX	:format_string="Amiga IFF / SVX8 / SV16 format"; break;
	case SF_FORMAT_NIST	:format_string="Sphere NIST format"; break;
	case SF_FORMAT_VOC	:format_string="VOC files"; break;
	case SF_FORMAT_IRCAM	:format_string="Berkeley/IRCAM/CARL"; break;
	case SF_FORMAT_W64	:format_string="Sonic Foundry's 64 bit RIFF/WAV"; break;
	case SF_FORMAT_MAT4	:format_string="Matlab (tm) V4.2 / GNU Octave 2.0"; break;
	case SF_FORMAT_MAT5	:format_string="Matlab (tm) V5.0 / GNU Octave 2.1"; break;
	case SF_FORMAT_PVF	:format_string="Portable Voice Format"; break;
	case SF_FORMAT_XI	:format_string="Fasttracker 2 Extended Instrument"; break;
	case SF_FORMAT_HTK	:format_string="HMM Tool Kit format"; break;
	case SF_FORMAT_SDS	:format_string="Midi Sample Dump Standard"; break;
	case SF_FORMAT_AVR	:format_string="Audio Visual Research"; break;
	case SF_FORMAT_WAVEX	:format_string="MS WAVE with WAVEFORMATEX"; break;
	case SF_FORMAT_SD2	:format_string="Sound Designer 2"; break;
	case SF_FORMAT_FLAC	:format_string="FLAC lossless file format"; break;
	case SF_FORMAT_CAF	:format_string="Core Audio File format"; break;
	case SF_FORMAT_WVE	:format_string="Psion WVE format"; break;
	case SF_FORMAT_OGG	:format_string="Xiph OGG container"; break;
	case SF_FORMAT_MPC2K	:format_string="Akai MPC 2000 sampler"; break;
	case SF_FORMAT_RF64	:format_string="RF64 WAV file"; bytes=8; break;
	case SF_FORMAT_OPUS	:format_string="Opus (RFC6716)"; break;
	case SF_FORMAT_MP3	:format_string="MPEG Layer 3 (mp3)"; break;

	default :
		format_string="unknown format!";
		break ;
	};

	char *sub_format_string;

	switch (sf_info.format & SF_FORMAT_SUBMASK)
	{
	//subtypes from here on
	case SF_FORMAT_PCM_S8	: sub_format_string="Signed 8 bit data"; bytes=1; break;
	case SF_FORMAT_PCM_16	: sub_format_string="Signed 16 bit data"; bytes=2; break;
	case SF_FORMAT_PCM_24	: sub_format_string="Signed 24 bit data"; bytes=3; break;
	case SF_FORMAT_PCM_32	: sub_format_string="Signed 32 bit data"; bytes=4; break;

	case SF_FORMAT_PCM_U8	: sub_format_string="Unsigned 8 bit data (WAV and RAW only)"; bytes=1; break;

	case SF_FORMAT_FLOAT	: sub_format_string="32 bit float data"; bytes=4; break;
	case SF_FORMAT_DOUBLE	: sub_format_string="64 bit float data"; bytes=8; break;

	case SF_FORMAT_ULAW	: sub_format_string="U-Law encoded"; break;
	case SF_FORMAT_ALAW	: sub_format_string="A-Law encoded"; break;
	case SF_FORMAT_IMA_ADPCM: sub_format_string="IMA ADPCM"; break;
	case SF_FORMAT_MS_ADPCM : sub_format_string="Microsoft ADPCM"; break;

	case SF_FORMAT_GSM610	: sub_format_string="GSM 6.10 encoding"; break;
	case SF_FORMAT_VOX_ADPCM: sub_format_string="Oki Dialogic ADPCM encoding"; break;

	case SF_FORMAT_G721_32	: sub_format_string="32kbs G721 ADPCM encoding"; break;
	case SF_FORMAT_G723_24	: sub_format_string="24kbs G723 ADPCM encoding"; break;
	case SF_FORMAT_G723_40	: sub_format_string="40kbs G723 ADPCM encoding"; break;

	case SF_FORMAT_DWVW_12	: sub_format_string="12 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_16	: sub_format_string="16 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_24	: sub_format_string="24 bit Delta Width Variable Word encoding"; break;
	case SF_FORMAT_DWVW_N	: sub_format_string="N bit Delta Width Variable Word encoding"; break;

	case SF_FORMAT_DPCM_8	: sub_format_string="8 bit differential PCM (XI only)"; break;
	case SF_FORMAT_DPCM_16	: sub_format_string="16 bit differential PCM (XI only)"; break;

	case SF_FORMAT_VORBIS	: sub_format_string="Xiph Vorbis encoding"; break;
	default :
		sub_format_string="unknown subformat!";
		break ;
	};

	const char *duration_str;
	duration_str=sin_generate_duration_str(&sf_info);

	if(print)
	{
		fprintf(stderr,"format:      %s\n	     %s (0x%08X)\nduration:    %s (%"PRId64" frames)\nsample rate: %d\nchannels:    %d\n"
			,format_string, sub_format_string, sf_info.format
			,duration_str
			,sf_info.frames
			,sf_info.sample_rate
			,sf_info.channels
		);
	}

	return bytes;
}//end print_file_info

#endif
//EOF
