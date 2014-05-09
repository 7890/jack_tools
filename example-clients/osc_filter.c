#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <lo/lo.h>
#include <regex.h>

//tb/140421//tb/140424

//test filter (receive, analyze, modify, (re-)send)
//set path regex filter: /match_path s "pattern"

//gcc -o jack_osc_filter osc_filter.c `pkg-config --libs jack liblo`

//different styles of messages
//	messages directed to filter (iternal settings)
//		example: set filter pattern /match_path, /<osc_client_id>/match_path
//		example: shutdown /quit, /<osc_client_id>/quit
//	messages that are filtered/forwarded for adjacent filters

//midi ports
static jack_port_t* port_in;
static jack_port_t* port_out_positive;
static jack_port_t* port_out_negative;
jack_client_t* client;
char const client_name[32] = "osc_filter";
char osc_client_id[32] = "/osc_filter";

int strcmp();

void* buffer_in;

char* path;
char* types;

void* buffer_out_positive;
void* buffer_out_negative;

//regex patterns
char* s1_star = "[a-zA-Z0-9/_.-]*"; //[*]
char* s2_plus = "[a-zA-Z0-9_.-]*"; //[+]
char* s3_questionmark = "[a-zA-Z0-9_.-]"; //[?]
char* s4_hash = "[0-9]+"; //[#]
char* s5_percent = "[0-9]*[.][0-9]*"; //[%]

//default match all

char path_match_pattern[255]="[*]";
char *path_match_pattern_expanded;

//example: ^/a/b[#]/[*]x[?]$ ...

char * expand_regex(char *pat);
int easyregex(char *pat,char *str);
char * str_replace (const char *string, const char *substr, const char *replacement);

int starts_with(const char *str, const char *prefix)
{
	if(strncmp(str, prefix, strlen(prefix)) == 0)
	{
		return 1;
	}
	return 0;
}

int ends_with(char const * str, char const * suffix)//, int lenstr, int lensuf)
{
    	if( ! str && ! suffix )
	{
		return 1;
	}
    	if( ! str || ! suffix )
	{
		return 0;
	}
	int lenstr = strlen(str);
	int lensuf = strlen(suffix);
	return strcmp(str + lenstr - lensuf, suffix) == 0;
}

static int process (jack_nframes_t frames, void* arg)
{
//prepare receive buffer

	buffer_in = jack_port_get_buffer (port_in, frames);
	assert (buffer_in);

//prepare send buffers

	buffer_out_positive = jack_port_get_buffer(port_out_positive, frames);
	assert (buffer_out_positive);
	jack_midi_clear_buffer(buffer_out_positive);

	buffer_out_negative = jack_port_get_buffer(port_out_negative, frames);
	assert (buffer_out_negative);
	jack_midi_clear_buffer(buffer_out_negative);

//receive
	int msgCount = jack_midi_get_event_count (buffer_in);
	int i;
	//iterate over encapsulated osc messages
	for (i = 0; i < msgCount; ++i) 
	{
		jack_midi_event_t event;
		int r = jack_midi_event_get (&event, buffer_in, i);
		if (r == 0) 
		{
			path=lo_get_path(event.buffer,event.size);

			int len;

			if(
				!strcmp(path,"/match_path") || 
				(
					starts_with(path,osc_client_id) &&
					ends_with(path,"/match_path")
				)
			)
			{
				lo_message msg = lo_message_deserialise(event.buffer, event.size, NULL);
				types=lo_message_get_types(msg);

				//check types
				if(!strcmp(types,"s"))
				{
					lo_arg **argv = lo_message_get_argv(msg);

					strncpy(path_match_pattern,&argv[0]->s,sizeof(path_match_pattern)- 1);
					path_match_pattern_expanded=expand_regex(path_match_pattern);
					printf("path regex filter pattern set to %s\n%s\n",path_match_pattern,path_match_pattern_expanded);
				}

				lo_message_free(msg);
				continue;
			}

			int retval=regex(path_match_pattern_expanded,path);
			if(!retval)
			{
				printf("regex match! %s\n",path);
			}

			//path comparison match
			if(!strcmp(path,"/hello"))
			{
				////
			}

			lo_message msg = lo_message_deserialise(event.buffer, event.size, NULL);
			//printf("osc message (%i) size: %lu argc: %d\n",i+1,event.size,lo_message_get_argc(msg));

			types=lo_message_get_types(msg);
			//printf("types %s path %s\n",types,path);

			/*match types
			if(!strcmp(types,"fis"))
			{
			}*/

			lo_arg **argv = lo_message_get_argv(msg);
			//printf("test: parameter 1 (float) is: %f \n",argv[0]->f);
			//printf("test: parameter 2 (int) is: %i \n",argv[1]->i);
			//printf("test: parameter 3 (string) is: %s \n",&argv[2]->s);
			
			/*match arg
			if(argv[1]->i==123)
			{
			}*/			

			//add arg
			//lo_message_add_int32(msg,887766);

			//(re-)send

			size_t msg_size;
			void *pointer;

			//rewrite path
			//pointer=lo_message_serialise(msg,"/hello/jack/midi",NULL,&msg_size);

			pointer=lo_message_serialise(msg,path,NULL,&msg_size);

			if(!retval)
			{
				if(msg_size <= jack_midi_max_event_size(buffer_out_positive))
				{
					//write the serialized osc message to the midi buffer
					jack_midi_event_write(buffer_out_positive,0,pointer,msg_size);
				}
				else
				{
					fprintf(stderr,"available jack midi buffer size was too small! message lost\n");
				}
			}
			else 
			{
				if(msg_size <= jack_midi_max_event_size(buffer_out_negative))
				{
				//write the serialized osc message to the midi buffer
				jack_midi_event_write(buffer_out_negative,0,pointer,msg_size);
				}
				else
				{
					fprintf(stderr,"available jack midi buffer size was too small! message lost\n");
				}
			}

			//free resources
			lo_message_free(msg);
			free(pointer);
		}
	}
	return 0;
}

static void signal_handler(int sig)
{
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}

int main (int argc, char* argv[])
{
	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		printf ("could not create JACK client\n");
		return 1;
	}

	strcpy(osc_client_id,"/");
	strcat(osc_client_id,jack_get_client_name (client));
	strcat(osc_client_id,"/");

	printf("jack osc client id is: %s\n",osc_client_id);

	jack_set_process_callback (client, process, 0);

	port_in = jack_port_register (client, "midi_osc_input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	port_out_positive = jack_port_register (client, "midi_osc_output_positive", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	port_out_negative = jack_port_register (client, "midi_osc_output_negative", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (port_in == NULL || port_out_positive == NULL || port_out_negative == NULL) 
	{
		fprintf (stderr, "could not register port\n");
		return 1;
	}
	else
	{
		printf ("registered JACK ports\n");
	}

	path_match_pattern_expanded=expand_regex(path_match_pattern);

	fprintf(stderr,"path regex filter pattern: %s\n",path_match_pattern);
	fprintf(stderr,"change with: /match_path s \"<pattern>\"\n");
	fprintf(stderr,"placeholders:\n");
	fprintf(stderr,"[*]: %s\n",s1_star);
	fprintf(stderr,"[+]: %s\n",s2_plus);
	fprintf(stderr,"[?]: %s\n",s3_questionmark);
	fprintf(stderr,"[#]: %s\n",s4_hash);
	fprintf(stderr,"[%%]: %s\n",s5_percent);
	fprintf(stderr,"[&]: JACK client name\n");
	fprintf(stderr,"\nexample: /match_path s \"^/[&]/[*]$\"\n");

	fprintf(stderr,"[&] is the jack client name\n");

	int r = jack_activate (client);
	if (r != 0) 
	{
		fprintf (stderr, "could not activate client\n");
		return 1;
	}

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	/* run until interrupted */
	while (1) 
	{
		//sleep(1);
		usleep(100000);
	};

	jack_client_close(client);
	return 0;
}

//http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring
char * str_replace (const char *string, const char *substr, const char *replacement)
{
	char *tok = NULL;
	char *newstr = NULL;
	char *oldstr = NULL;
	//if either substr or replacement is NULL, duplicate string a let caller handle it
	if (substr == NULL || replacement == NULL)
	{
		return strdup (string);
	}
	newstr = strdup (string);
	while ((tok = strstr (newstr, substr)))
	{
		oldstr = newstr;
		newstr = malloc (strlen (oldstr) - strlen (substr) + strlen (replacement) + 1);

		//failed to alloc mem, free old string and return NULL
		if (newstr == NULL)
		{
			free (oldstr);
			return NULL;
		}
		memcpy (newstr, oldstr, tok - oldstr);
		memcpy (newstr + (tok - oldstr), replacement, strlen (replacement));
		memcpy (newstr + (tok - oldstr) + strlen(replacement), tok + strlen (substr), strlen (oldstr) - strlen (substr) - (tok - oldstr));
		memset (newstr + strlen (oldstr) - strlen (substr) + strlen (replacement) , 0, 1);
		free (oldstr);
	}
	return newstr;
}

int easyregex(char *pat,char *str)
{
	return regex(expand_regex(pat),str);
}

int regex(char *pat,char *str)
{
	regex_t regex;
	int retval = regcomp(&regex, pat, REG_EXTENDED);

	if(retval)
	{
		fprintf(stderr, "could not compile regex\n"); 
		return 1;
	}

	//execute regex
	retval = regexec(&regex, str, 0, NULL, 0);
	regfree(&regex);
	return retval;
}

char * expand_regex(char *pat)
{
	//create legal regex of simplified "regex" string
	char* s1 = str_replace(pat, "[*]", s1_star);
	char* s2 = str_replace(s1, "[+]", s2_plus);
	char* s3 = str_replace(s2, "[?]", s3_questionmark);
	char* s4 = str_replace(s3, "[#]", s4_hash);
	char* s5 = str_replace(s4, "[%]", s5_percent);
	char* s6 = str_replace(s5, "[&]", jack_get_client_name (client));
	free(s1);
	free(s2);
	free(s3);
	free(s4);
	free(s5);
	return s6;
}
