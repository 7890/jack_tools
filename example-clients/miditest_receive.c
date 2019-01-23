#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>

//gcc -o miditest_receive miditest_receive.c `pkg-config --libs jack`
//tb/14re19

jack_port_t *port_in;
void *buffer_in;
int quit_program=0;
void hexdump (const char *desc, void *addr, int len);

static void signal_handler(int sig)
{
	fprintf(stderr, "Shutting down\n");
	quit_program=1;
}

static int process(jack_nframes_t frames, void *arg)
{
	if(quit_program==1){return 0;}
	buffer_in=jack_port_get_buffer(port_in, frames);
	int msg_count=jack_midi_get_event_count(buffer_in);

	int i;
	//iterate over messages
	for(i=0; i < msg_count; ++i)
	{
		fprintf(stderr, ".");
		jack_midi_event_t event;
		int r=jack_midi_event_get(&event, buffer_in, i);
		if(r==0)
		{
			hexdump("event", event.buffer, event.size);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	jack_client_t *client;
	const char *client_name="miditest_receive";

	client=jack_client_open(client_name, JackNullOption, NULL);
	if(client==NULL)
	{
		fprintf(stderr, "Error: could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback(client, process, 0);

	port_in=jack_port_register(client, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	if(port_in==NULL)
	{
		fprintf(stderr, "Error: could not register port\n");
		return 1;
	}
	else
	{
		fprintf(stderr, "Registered JACK MIDI client input port 'in'\n");
	}
	

	if(jack_activate(client))
	{
		fprintf(stderr, "Error: cannot activate client");
		return 1;
	}

	signal(SIGINT, signal_handler);

	fprintf(stderr, "Ready\n");

	while(quit_program==0)
	{
		sleep(1);
	}

//	jack_port_unregister(client, port_in);
	jack_deactivate(client);
	jack_client_close(client);
	return 0;
}

//http://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
void hexdump (const char *desc, void *addr, int len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;

	// Output description if given.
	if (desc != NULL)
		printf ("%s\n", desc);

	if (len == 0)
	{
		printf("  ZERO LENGTH\n");
		return;
	}
	if (len < 0)
	{
		printf("  NEGATIVE LENGTH: %i\n",len);
		return;
	}

	// Process every byte in the data.
	for (i = 0; i < len; i++)
	{
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0)
		{
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				printf ("  %s\n", buff);

			// Output the offset.
			printf ("  %04x ", i);
		}

		// Now the hex code for the specific character.
		printf (" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0)
	{
		printf ("   ");
		i++;
	}

	// And print the final ASCII bit.
	printf ("  %s\n", buff);
}
//EOF
