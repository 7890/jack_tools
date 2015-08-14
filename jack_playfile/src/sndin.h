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

#include <sndfile.h>
#include <opusfile.h>
#include <vorbis/vorbisfile.h>
//should be optional
#include <mpg123.h>

#include "config.h"

typedef struct
{
	sf_count_t frames; //Used to be called samples.
	int samplerate;
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

//if found to be mp3 file
static int is_mpg123=0;

//yet another reader
static OggOpusFile *soundfile_opus;

//if found to be opus file
static int is_opus=0;

//..and another (for faster seeking)
static OggVorbis_File soundfile_vorbis;

//oggvorbis takes a regular file
FILE *ogg_file_;

//if found to be opus file
static int is_ogg_=0;

//multichannel float buffer for ov_read_float
static float **ogg_buffer;

static int is_flac_=0;

//how many bytes one sample (of one channel) is using in the file
static int bytes_per_sample_native=0;

//how many frames to read per request
static int sndfile_request_frames=0;

//readers read into this buffer
static float *frames_from_file_buffer;

//reported by stat()
static uint64_t file_size_bytes=0;

//derive from sndfile infos or estimate based on filesize & framecount
static float file_data_rate_bytes_per_second=0;

static uint64_t total_bytes_read_from_file=0;

static uint64_t total_frames_read_from_file=0;

//in disk thread, detect if 'frame_count' was read from file
static int all_frames_read=0;

static int sin_open(const char *fileuri, SF_INFO_GENERIC *sf_info);
static sf_count_t sin_seek(sf_count_t offset, int whence);
static void sin_close();

static int64_t read_frames_from_file_to_buffer(uint64_t count, float *buffer);

static double frames_to_seconds(sf_count_t frames, int samplerate);
static double get_seconds(SF_INFO_GENERIC *sf_info);
static const char *format_duration_str(double seconds);
static const char *generate_duration_str(SF_INFO_GENERIC *sf_info);

static int is_flac(SF_INFO_GENERIC *sf_info);
static int is_ogg(SF_INFO_GENERIC *sf_info);

static int file_info(SF_INFO_GENERIC sf_info, int print);

//read full-channel from file to this buffer before sorting out channel_offset and channel_count
static float *tmp_buffer=new float[10000000]; ////

//result of requested channel_offset, channel_count and channels in file
int channel_count_use_from_file=0;

//pseudo "lock"
int closing_file_in_progress=0;

//return 0 on error, 1 on success
//=============================================================================
static int sin_open(const char *fileuri, SF_INFO_GENERIC *sf_info)
{
	is_opus=0;
	is_ogg_=0;
	is_mpg123=0;
	is_flac_=0;

	memset (&sf_info_sndfile, 0, sizeof (sf_info_sndfile)) ;
	/*
	typedef struct
	{ sf_count_t  frames ; //Used to be called samples.
	int	samplerate ;
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
		sf_info_generic.samplerate=sf_info_sndfile.samplerate;
		sf_info_generic.channels=sf_info_sndfile.channels;
		sf_info_generic.format=sf_info_sndfile.format;

		//use vorbisfile to decode ogg
		if(is_ogg(&sf_info_generic))
		{
			sf_close(soundfile);

			ogg_file_  = fopen(fileuri,"r");
			ov_open(ogg_file_,&soundfile_vorbis,NULL,0);
			is_ogg_=1;
		}
	}
	else
	{
		//try opus
		int ret;
		soundfile_opus=op_open_file(fileuri,&ret);

		if(soundfile_opus!=NULL)
		{
			is_opus=1;

			//seek to end, get frame count
			sf_info_generic.frames=op_pcm_total(soundfile_opus,-1);
			//the libopusfile API always decodes files to 48 kHz.
			sf_info_generic.samplerate=48000;
			sf_info_generic.channels=op_channel_count(soundfile_opus,0);
			sf_info_generic.format=SF_FORMAT_OPUS | SF_FORMAT_FLOAT;
		}
		else
		{
			//try mp3
			mpg123_init();
			soundfile_123=mpg123_new(NULL, NULL);

			mpg123_format_none(soundfile_123);
			int format=mpg123_format(soundfile_123
				,48000
				,MPG123_STEREO
				,MPG123_ENC_FLOAT_32
			);

			int ret = mpg123_open(soundfile_123, fileuri);
			if(ret == MPG123_OK)
			{
				long rate;
				int channels, format;
				mpg123_getformat(soundfile_123, &rate, &channels, &format);

				struct mpg123_frameinfo mp3info;
				mpg123_info(soundfile_123, &mp3info);

				if(format==0)
				{
					fprintf (stderr, "/!\\ cannot open file \"%s\"\n", fileuri);
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
				sf_info_generic.samplerate=48000; ///
				sf_info_generic.channels=2; ///
				sf_info_generic.format=SF_FORMAT_MP3 | SF_FORMAT_FLOAT;
			}
			else 
			{
				fprintf (stderr, "/!\\ cannot open file \"%s\"\n", fileuri);
				return 0;
			}
		}//end try mpg123
	}//end try opus

	//matching format found
	return 1;
}//end sin_open

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
			return mpg123_seek(soundfile_123,offset,whence);
		}
	}
	else if(is_opus)
	{
		if(whence==SEEK_SET)
		{
			return op_pcm_seek(soundfile_opus,offset);
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
	else if(is_ogg_)
	{
		if(whence==SEEK_SET)
		{
			return ov_pcm_seek(&soundfile_vorbis,offset);
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
			soundfile_123=NULL;
		}
	}
	else if(is_opus)
	{
		if(soundfile_opus!=NULL)
		{
			op_free(soundfile_opus);
			soundfile_opus=NULL;
		}
	}
	else if(is_ogg_)
	{
		if(ogg_file_!=NULL)
		{
			ov_clear(&soundfile_vorbis);
			ogg_file_=NULL;
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
static int64_t read_frames_from_file_to_buffer(uint64_t count, float *buffer)
{
	int64_t frames_to_go=count;
	int64_t could_read_frame_count=0;

	int file_eof=0;

	int ogg_buffer_offset=0;
	int current_section;

//	fprintf(stderr,"\nread_frames_from_file_to_buffer() called\n");

	while(frames_to_go>0 && !file_eof)
	{
//		fprintf(stderr,"\nto go %"PRId64"\n",frames_to_go);

		//==================================== sndfile
		if(!is_mpg123 && !is_opus && !is_ogg_)
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
		//==================================== opus
		else if(is_opus)
		{
			//read multichannel as normal
			could_read_frame_count=op_read_float(soundfile_opus
				,(float*)tmp_buffer
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
				tmp_buffer+=could_read_frame_count*sf_info_generic.channels;

//				fprintf(stderr,"\ncould read %"PRId64", to go %"PRId64"\n",could_read_frame_count,frames_to_go);
				continue;
			}
		}//end opus
		//==================================== ogg
		else if(is_ogg_)
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
		//==================================== mpg123
		else if(is_mpg123)
		{
			size_t fill=0;
			mpg123_read(soundfile_123,(unsigned char*)tmp_buffer
				,frames_to_go*sf_info_generic.channels*bytes_per_sample, &fill);

			if(fill<=0)
			{
//				fprintf(stderr,"\ncould not read, return was %"PRId64"\n",fill);
				file_eof=1;
				break;
			}
			else
			{
				could_read_frame_count=fill/(sf_info_generic.channels*bytes_per_sample);
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

	if(is_opus)
	{
		//"reset" back to start
		tmp_buffer-=(count-frames_to_go)*sf_info_generic.channels;
	}


	//filter out unwanted channels, only copy requested
	if(channel_count_use_from_file>=0)
	{
		int bindex=0;
		for(int i=0;i<(count-frames_to_go);i++)
		{
			for(int k=0;k<sf_info_generic.channels;k++)
			{
				if(k>=channel_offset && k<channel_offset+channel_count_use_from_file)
				{
					buffer[bindex]=tmp_buffer[i*sf_info_generic.channels+k];
					bindex++;
//					fprintf(stderr,"\n+");
				}
				else
				{
//					fprintf(stderr,"\n-");
				}

			}
		}
	}//end if(channel_count_use_from_file>=0)

	//in case of a normal, full buffer read, this will be equal to count
	return count-frames_to_go;
}//end read_frames_from_file_to_buffer

//=============================================================================
static double frames_to_seconds(sf_count_t frames, int samplerate)
{
	double seconds;
	if (frames==0)
	{
		return 0;
	}
	seconds = (1.0 * frames) / samplerate;
	return seconds;
}

//=============================================================================
static double get_seconds(SF_INFO_GENERIC *sf_info)
{
	double seconds;
	if (sf_info->samplerate < 1)
	{
		return 0;
	}
	if (sf_info->frames / sf_info->samplerate > 0x7FFFFFFF)
	{
		return -1;
	}
	seconds = (1.0 * sf_info->frames) / sf_info->samplerate;
	return seconds;
}

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char *format_duration_str(double seconds)
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

//=============================================================================
//https://github.com/erikd/libsndfile/blob/master/programs/sndfile-info.c
static const char *generate_duration_str(SF_INFO_GENERIC *sf_info)
{
	return(
		format_duration_str(
			get_seconds(sf_info)
		)
	);
}

//=============================================================================
static int is_flac(SF_INFO_GENERIC *sf_info)
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
static int is_ogg(SF_INFO_GENERIC *sf_info)
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
static int file_info(SF_INFO_GENERIC sf_info, int print)
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
	duration_str=generate_duration_str(&sf_info);

	if(print)
	{
		fprintf(stderr,"format:      %s\n	     %s (0x%08X)\nduration:    %s (%"PRId64" frames)\nsample rate: %d\nchannels:    %d\n"
			,format_string, sub_format_string, sf_info.format
			,duration_str
			,sf_info.frames
			,sf_info.samplerate
			,sf_info.channels
		);
	}

	return bytes;
}//end print_file_info

#endif
//EOF
