/*
  (c) Kjetil Matheussen, 2005/2006.
  (c) Jonathan Woithe, 2006.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

//tb/150508
/*
adjust Makefile, formatting noops, replace deprecated jack_client_new, use weak_libjack
*/

#include "jack_play.h"

/* Arguments and their default values */
static float buffer_time=5;
static jack_client_t *client=NULL;
static unsigned int channels=-1; // Unless any --port arguments have been specified, the default is 2, and set in portnames_add_defaults()
static char *filename=NULL;
static double play_time=0.0;

/* JACK data */
static jack_port_t **ports;
//typedef jack_default_audio_sample_t sample_t;
static int jack_buffer_size;
static int jack_buffer_size_is_changed_to=0;
static float jack_samplerate;
//static int connect_num=0;

/* Disk thread */
static pthread_t disk_thread={0};
static pthread_mutex_t disk_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;
static int file_nchannels = 0;
static int disk_thread_finished = 0;

/* Synchronization between process thread and disk thread. */
static volatile int is_initialized=0; // This $@#$@#$ variable is needed because jack ports must be initialized _after_ (???) the client is activated. (stupid jack)
static volatile int is_running=1;
static jack_ringbuffer_t *rb=NULL;
//struct ringbuffer_block
//{
//	sample_t *buffer;
//};

/* Jack connecting thread. */
static pthread_t connect_thread={0} ;
static pthread_cond_t  connect_ready = PTHREAD_COND_INITIALIZER;

static pid_t mainpid;

/////////////////////////////////////////////////////////////////////
//////////////////////// BUFFERS ////////////////////////////////////
/////////////////////////////////////////////////////////////////////

static sample_t **buffers=NULL;
static sample_t *empty_buffer=NULL;

static int num_buffers;
static int buffer_size;
static int buffer_size_in_bytes;

//=============================================================================
static void buffers_init(int jackbuffersize)
{
	if(jack_buffer_size!=jackbuffersize)
	{
		printf("(Reallocating buffers to fit jack buffer size of %d)\n",jackbuffersize);
	}

	// Samplebuffers;
	static sample_t *das_buffer=NULL;
	int lokke;

	free(das_buffer);
	free(buffers);
	free(empty_buffer);

	buffer_size=channels*jackbuffersize;
	buffer_size_in_bytes=buffer_size*sizeof(sample_t);

	num_buffers=2 + (
		buffer_time*jack_samplerate*sizeof(sample_t)*channels 
		/ buffer_size_in_bytes);

	// Allocate Ringbuffer
	if(rb!=NULL)
	{
		jack_ringbuffer_free(rb);
	}
	  
	rb=jack_ringbuffer_create(sizeof(struct ringbuffer_block)*num_buffers);
	//printf("ringbuffer_size: %d, num_buffers: %d\n",rb->size/4,num_buffers);
	  
	/* When JACK is running realtime, jack_activate() will have
	 * called mlockall() to lock our pages into memory.  But, we
	 * still need to touch any newly allocated pages before
	 * process() starts using them.  Otherwise, a page fault could
	 * create a delay that would force JACK to shut us down. (from jackrec, -Kjetil)*/
	memset(rb->buf,0,rb->size);

	//The size of the ringbuffer must be less than num_buffers, but jack_ringbuffer_create allocates
	//up to the nearest 2^n number. Therefore we need to set num_buffers again.
	num_buffers=rb->size/sizeof(struct ringbuffer_block);
	num_buffers+=3;

	//printf("buffer_size: %d, buffer_size_in_bytes: %d, num_buffers: %d, total: %d, buffer_time: %f\n",
	//	   buffer_size,buffer_size_in_bytes,num_buffers,num_buffers*buffer_size_in_bytes,buffer_time);

	das_buffer=calloc(num_buffers,buffer_size_in_bytes);
	buffers=calloc(sizeof(sample_t*),num_buffers);

	for(lokke=0;lokke<num_buffers;lokke++)
	{
		buffers[lokke]=das_buffer+(lokke*buffer_size);
	}

	empty_buffer=calloc(1,buffer_size_in_bytes);

	jack_buffer_size=jackbuffersize;
	jack_buffer_size_is_changed_to=0;
}

//=============================================================================
static sample_t *buffer_get(void)
{
	static int next_free_buffer=0;
	sample_t *ret=buffers[next_free_buffer];
	next_free_buffer++;
	if(next_free_buffer==num_buffers)
	{
		next_free_buffer=0;
	}
	return ret;
}

//=============================================================================
int buffersizecallback(size_t newsize,void *arg)
{
	jack_buffer_size_is_changed_to=newsize;
	if(pthread_mutex_trylock(&disk_thread_lock)==0)
	{
		pthread_cond_signal(&data_ready);
		pthread_mutex_unlock(&disk_thread_lock);
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////
//////////////////////// PORTNAMES //////////////////////////////////
/////////////////////////////////////////////////////////////////////

static char **cportnames=NULL;
static int num_cportnames=0; // After initialization, this variable is not used anymore and "channels" is used instead.
                             // This is because "channels" can be set on the commandline.
//=============================================================================
static int findnumports(char **ports)
{
	int ret=0;
	while(ports && ports[ret]!=NULL)
	{
		ret++;
	}
	return ret;
}

//=============================================================================
static void portnames_add_defaults(void)
{
	if(cportnames==NULL)
	{
		cportnames=(char **)jack_get_ports(client,NULL,NULL,JackPortIsPhysical|JackPortIsInput);
		num_cportnames=JC_MIN(2,findnumports(cportnames));
	}
	if(channels==-1)
	{
		channels=num_cportnames;
	}
	else
	{
		num_cportnames=JC_MIN(channels,num_cportnames);
		channels=num_cportnames;
	}
	if(channels==0)
	{
		printf("No valid ports selected.\n");
		exit(0);
	}
}

//=============================================================================
static void portnames_add(char *name)
{
	char **outportnames=(char**)jack_get_ports(client,name,"",0);
	int ch=findnumports(outportnames);

	if(ch>0)
	{
		cportnames=realloc(cportnames,(num_cportnames+ch)*sizeof(char*));

		ch=0;
		while(outportnames[ch]!=NULL)
		{
			cportnames[num_cportnames]=outportnames[ch];
			ch++;
			num_cportnames++;
		}

	}
	else
	{
		printf("\nWarning, No ports with name \"%s\".\n",name);
		if(cportnames==NULL)
		{
			printf("This could lead to using default ports instead.\n");
		}
	}
}

//=============================================================================
static char **portnames_get_connections(int ch)
{
	jack_port_t* port=jack_port_by_name(client,cportnames[ch]);
	char **ret;

	if(jack_port_flags(port) & JackPortIsOutput)
	{
		ret=(char**)jack_port_get_all_connections(client,port);
	}
	else
	{
		ret=calloc(2,sizeof(char*));
		ret[0]=cportnames[ch];
	}
	
	return ret;
}


/////////////////////////////////////////////////////////////////////
//////////////////////// DISK ///////////////////////////////////////
/////////////////////////////////////////////////////////////////////

//=============================================================================
static int disk_read(SNDFILE *soundfile,sample_t *buffer,size_t frames)
{
	sf_count_t f;
	f = sf_readf_float(soundfile,buffer,frames);
	if (f != 0)
	{
		// A partial read can occur at the end of the file.	Zero out
		// any part of the buffer not written by sf_readf_float().
		if (f != frames)
		{
			bzero(buffer+f*file_nchannels,(frames-f)*file_nchannels*sizeof(buffer[0]));
		}
		return 1;
	}

	// If no frames were read assume we're at EOF
	return 0;
}

//=============================================================================
static unsigned long long underruns=0;

//=============================================================================
static void *disk_thread_func (void *arg)
{
	SNDFILE *soundfile;

	// Init soundfile
	SF_INFO sf_info={0};

	soundfile=sf_open(filename,SFM_READ,&sf_info);
	if (soundfile == NULL)
	{
		fprintf (stderr, "\ncannot open sndfile \"%s\" for input (%s)\n", filename,sf_strerror(NULL));
		jack_client_close(client);
		exit(1);
	}
	file_nchannels = sf_info.channels;

	if (sf_info.channels != channels)
	{
		fprintf(stderr, "Error: file has %d channels while %d ports were configured\n",
		sf_info.channels, channels);
		jack_client_close(client);
		exit(1);
	}

	if (sf_info.samplerate != jack_get_sample_rate(client))
	{
		fprintf(stderr, "Warning: file sample rate (%d Hz) different from JACK rate (%d Hz)\n",
		sf_info.samplerate, jack_get_sample_rate(client));
	}

	// Main disk loop
	for(;;)
	{
		while( (jack_ringbuffer_write_space (rb) >= sizeof(struct ringbuffer_block)))
		{
			struct ringbuffer_block block;
			sample_t *buffer;
			if(is_running==0)
			{
				goto done;
			}

			buffer=buffer_get(); // This won't fail because the size of the ringbuffer is less than the number of buffers.

			// disk_read() returns 0 on EOF
			if (!disk_read(soundfile,buffer,jack_buffer_size)) 
			{
				goto done;
			}
			block.buffer = buffer;
			jack_ringbuffer_write(rb,(void*)&block,sizeof(struct ringbuffer_block));
		}
	      
		if(jack_buffer_size_is_changed_to>0)
		{
			buffers_init(jack_buffer_size_is_changed_to);
		}
		/* wait until process() signals that more data is required */
		pthread_cond_wait (&data_ready, &disk_thread_lock);
	}
done:

	// Close soundfile
	sf_close (soundfile);

	pthread_mutex_unlock (&disk_thread_lock);
	printf ("disk thread finished\n");

	disk_thread_finished = 1;
	return 0;
}

//=============================================================================
void setup_disk_thread (void)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&disk_thread_lock);
	pthread_create(&disk_thread, NULL, disk_thread_func, NULL);
}

//=============================================================================
void stop_disk_thread(void)
{
 /* Wake up disk thread. (no trylock) (isrunning==0)*/
	pthread_cond_signal(&data_ready);
	pthread_join(disk_thread, NULL);
}


/////////////////////////////////////////////////////////////////////
//////////////////////// JACK ///////////////////////////////////////
/////////////////////////////////////////////////////////////////////	

//=============================================================================
void put_buffer_into_jack_buffers(sample_t *buffer)
{
	jack_default_audio_sample_t *out[channels];
	int ch;
	int i,pos=0;
	
	for(ch=0;ch<channels;ch++)
	{
		out[ch]=jack_port_get_buffer(ports[ch],jack_buffer_size);
	}

	if (buffer != NULL)
	{
		for(i=0;i<jack_buffer_size;i++)
		{
			for(ch=0;ch<channels;ch++)
			{
				out[ch][i] = buffer[pos++];
			}
		}
	} 
	else
	{
		for(ch=0;ch<channels;ch++)
		{
			bzero(out[ch],sizeof(out[ch][0])*jack_buffer_size);
		}
	}
}

//=============================================================================
void req_buffer_from_disk_thread()
{
	if (pthread_mutex_trylock (&disk_thread_lock) == 0)
	{
		pthread_cond_signal (&data_ready);
		pthread_mutex_unlock (&disk_thread_lock);
	}
}

//=============================================================================
int process (jack_nframes_t nframes, void *arg)
{
	static int killed = 0;
	if(is_initialized==0)
	{
		return 0;
	}

	if((jack_buffer_size_is_changed_to != 0) // The inserted silence will have the wrong size, but at least the user will get a report that something was not recorded.
		 || jack_ringbuffer_read_space(rb)<sizeof(struct ringbuffer_block))
	{
		if (disk_thread_finished)
		{
			// Isn't there an neater way of shutting things down?	Ensure only
			// one attempt is made to send the kill signal.
			if (!killed)
			{
				kill(mainpid, SIGINT);
				killed = 1;
			}
		}
		else
		{
				underruns++;
		}
		put_buffer_into_jack_buffers(NULL);
	}
	else
	{
		struct ringbuffer_block block;

		jack_ringbuffer_read(rb,(void*)&block,sizeof(struct ringbuffer_block));
		put_buffer_into_jack_buffers(block.buffer);
	}
	if (!disk_thread_finished)
	{
		req_buffer_from_disk_thread();
	}
	return 0;
}


/////////////////////////////////////////////////////////////////////
/////////////////// JACK CONNECTIONS ////////////////////////////////
/////////////////////////////////////////////////////////////////////

//=============================================================================
static pthread_mutex_t connect_thread_lock = PTHREAD_MUTEX_INITIALIZER;

//=============================================================================
static int compaire(const void *a, const void *b)
{
	return strcmp((const char*)a,(const char*)b);
}

//=============================================================================
static int reconnect_ports_questionmark(void)
{
	int ch;
	for(ch=0;ch<channels;ch++)
	{
		char **connections1 = portnames_get_connections(ch);
		const char **connections2 = jack_port_get_all_connections(client,ports[ch]);
		int memb1=findnumports(connections1);
		int memb2=findnumports((char**)connections2);

		if(memb1!=memb2)
		{
			free(connections1);
			free(connections2);
			return 1;
		}
		
		qsort(connections1,memb1,sizeof(char*),compaire);
		qsort(connections2,memb1,sizeof(char*),compaire);
		
		int lokke = 0;
		for(lokke=0;lokke<memb1;lokke++)
		{
			if(strcmp(connections1[lokke],connections2[lokke]))
			{
				free(connections1);
				free(connections2);
				return 1;
			}
		}

		free(connections1);
		free(connections2);
	}
	return 0;
}

//=============================================================================
static void disconnect_ports(void)
{
	int ch;
	for(ch=0;ch<channels;ch++)
	{
		int lokke = 0;
		const char **connections = jack_port_get_all_connections(client,ports[ch]);
		while(connections && connections[lokke] != NULL)
		{
			jack_disconnect(client,connections[lokke],jack_port_name(ports[ch]));
			lokke++;
		}
		free(connections);
	}
}

//=============================================================================
static void connect_ports(void)
{
	int ch;
	for(ch=0;ch<channels;ch++)
	{
		int lokke = 0;
		char **connections = portnames_get_connections(ch);
		while(connections && connections[lokke] != NULL)
		{
			int err=jack_connect(client,jack_port_name(ports[ch]),connections[lokke]);
			if(err!=0)
			{
				fprintf(stderr, "\ncannot connect output port %s to %s, errorcode %s\n", jack_port_name (ports[ch]), connections[lokke],strerror(err));
			}
			lokke++;
		}
		free(connections);
	}
}

//=============================================================================
static void* connection_thread(void *arg)
{
	while(1)
	{
		pthread_cond_wait(&connect_ready, &connect_thread_lock);
		if(is_running==0)
		{
			goto done;
		}
		if(is_initialized && reconnect_ports_questionmark())
		{
			printf("\n(Reconnecting ports)\n");
			disconnect_ports();
			connect_ports();
		}
	}
done:
	pthread_mutex_unlock(&connect_thread_lock);
	printf("connection thread finished\n");
	return NULL;
}

//=============================================================================
static void wake_up_connection_thread(void)
{
	// Don't want trylock here.
	pthread_cond_signal (&connect_ready);
}

//=============================================================================
static void start_connection_thread(void)
{
	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock (&connect_thread_lock);
	pthread_create(&connect_thread,NULL,connection_thread,NULL);
}

//=============================================================================
static void stop_connection_thread(void)
{
	wake_up_connection_thread();
	pthread_join(connect_thread, NULL);
}

//=============================================================================
static int graphordercallback(void *arg)
{
	wake_up_connection_thread();
	return 0;
}

//=============================================================================
static void create_ports(void)
{
	ports = (jack_port_t **) calloc (sizeof (jack_port_t *),channels);	
	{
		int ch;
		for(ch=0;ch<channels;ch++)
		{
			char name[500];
			sprintf(name,"output%d",ch+1);
			ports[ch]=jack_port_register(client,name,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
			if(ports[ch]==0)
			{
				fprintf(stderr,"\ncannot register output port \"%s\"!\n", name);
				jack_client_close(client);
				exit(1);
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////
/////////////////// INIT / SHUTDOWN /////////////////////////////////
/////////////////////////////////////////////////////////////////////

//=============================================================================
static void do_exit(int close_jack)
{
	is_running=0;

	stop_disk_thread();

	if(close_jack) // Don't close if called from jack_shutdown. (its already closed)
	{
		jack_client_close(client);
	}

	stop_connection_thread();
}

//=============================================================================
// static pid_t mainpid;
static void finish(int sig)
{
	if(getpid()==mainpid)
	{
		printf("I'm signaled to finish. Please wait while cleaning up.\n");
		do_exit(1);
		exit(0);
	}
}

//=============================================================================
static void jack_shutdown(void *arg)
{
	fprintf (stderr, "\nJACK shutdown.\n");
	do_exit(0);
	exit(0);
}

//=============================================================================
static jack_client_t *new_jack_client(char *name)
{
//	jack_client_t *client=jack_client_new(name);

	jack_options_t jack_opts = JackNoStartServer;
	jack_status_t status;
	const char *server_name="default";

	//open a client connection to the JACK server
	jack_client_t *client=jack_client_open(name, jack_opts, &status, server_name);
	if(client==NULL) 
	{
		fprintf(stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		if(status & JackServerFailed) 
		{
			fprintf(stderr, "Unable to connect to JACK server\n");
		}
		exit(1);
	}

	name=jack_get_client_name(client);

/*
	if(client==NULL)
	{
		for(connect_num=1;connect_num<100;connect_num++)
		{
			char temp[500];
			sprintf(temp,"%s_%d",name,connect_num);
			client=jack_client_new(temp);
			if(client!=NULL) return client;
		}
		fprintf(stderr, "\njack server not running? (Unable to create jack client \"%s\")\n",name);
		exit(1);
	}
*/
	return client;
}

//=============================================================================
static void start_jack(void)
{
	static int I_am_already_called=0;
	if(I_am_already_called) // start_jack is called more than once if the --port argument has been used.
	{
		return;
	}

	client=new_jack_client("jack_play");

	jack_buffer_size=jack_get_buffer_size(client);
	jack_samplerate=jack_get_sample_rate(client);
	I_am_already_called=1;
}

/////////////////////////////////////////////////////////////////////
/////////////////// MAIN ////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

//=============================================================================
int main (int argc, char *argv[])
{
	mainpid=getpid();
	// Arguments
	OPTARGS_BEGIN("Usage: jack_play  [--bufsize seconds] [--channels n]  [--port port] <filename>\n"
		  "                  [-B seconds]        [ -c n]         [ -p port]\n"
		  "\n"
		  "\"Filename\" has no default and must be specified\n"
		  "\"Bufsize\"  is by default 5 seconds.\n"
		  "\"Port\"     is by default set to all ports connected to the physical outputs.\n"
		  "\n"
		  "\n"
		  "Additional arguments:\n"
		  "[--playing-time s] or [-d s]     -> Playing is stopped after \"s\" seconds.\n"
		  "\n"
		  "Examples:\n"
		  "\n"
		  "To play a stereo file:\n"
		  "  $jack_play foobar.wav\n"
		  "\n"
		  "To play a stereo file (if alsa is used):\n"
		  "  $jack_play --port alsa_pcm:playback_1 --port alsa_pcm:playback_2 foobar.wav\n"
		  "\n"
		  "Same as above, but shorter syntax:\n"
		  "  $jack_play --channels 2 --port alsa_pcm:playback*\n"
		  "\n"
		  "To play in to jamin:\n"
		  "  $jack_play --port jamin:in* sound_to_jamin.wav\n"
		  "\n"
		  "Version 0.2\n"
		  )
	{
		OPTARG("--bufsize","-B") buffer_time = OPTARG_GETFLOAT();
		OPTARG("--channels","-c") channels = OPTARG_GETINT();
		OPTARG("--playing-time","-d") play_time = OPTARG_GETFLOAT();
		OPTARG("--port","-p") start_jack() ; portnames_add(OPTARG_GETSTRING());
		OPTARG_LAST() filename=OPTARG_GETSTRING();
	}
	OPTARGS_END;

	// Filename
	if(filename==NULL)
	{
		fprintf(stderr,"Error: please specify an input file\n");
		exit(1);
	}

	// Init jack 1
	start_jack();
	portnames_add_defaults();

	//
	buffers_init(jack_buffer_size);

	//
	setup_disk_thread ();

	// Init jack 2
	jack_set_process_callback(client, process, NULL);
	jack_on_shutdown(client, jack_shutdown, NULL);

	jack_set_graph_order_callback(client,graphordercallback,NULL);
	jack_set_buffer_size_callback(client,buffersizecallback,NULL);

	if (jack_activate(client))
	{
	  fprintf (stderr,"\ncannot activate client");
	}
   
	create_ports();
	connect_ports();
	start_connection_thread();

	// Everything initialized.
	//   (The threads are waiting for this variable, not the other way around, so now it just needs to be set.)
	is_initialized=1;

	signal(SIGINT,finish);

	if(play_time>0.0)
	{
		printf("Playing from \"%s\". The playback is going to last %lf seconds. Press <Ctrl-C> to stop before that.\n",filename,play_time);  
		sleep(play_time);
		usleep( (play_time-floor(play_time)) * 1000000);
	}
	else
	{
		// Wait for <return> or SIGINT
		printf("Playing from \"%s\". Press <Return> or <Ctrl-C> to stop it.\n",filename);		

		char gakk[64];
		fgets(gakk,49,stdin);

		if (underruns > 0)
		{
			fprintf (stderr,
				"\nWarning: jack_play failed with a total of %llu underruns.\n", underruns);
			fprintf (stderr, " try a bigger buffer than -B %f\n",buffer_time);
		}
		printf("Please wait for cleanup. (shouldn't take long)\n");
	}

	//
	do_exit(1);

	return 0;
}//end main

