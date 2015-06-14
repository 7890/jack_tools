// This programme captures audio from 8 sources then writes them to a 8 channel wav file called 'capture'wav'
// It is intended to demonstrate using JACK and FFADO with a Focusrite Saffire Pro 10 I/O
//
// It is compiled using the following command
// gcc -Wall -o wavCapture wavCapture.c -lsndfile -ljack -lpthread -lrt
//
// A.Greensted
// September 2010

//tb/1506
//whenever looking again for good JACK example code i come back to this file
//downloaded from http://www.labbookpages.co.uk/audio/files/saffireLinux/wavCapture.c
//Sun Jun 14 15:45:28 CEST 2015
//kept for reference

#define _GNU_SOURCE	// Declare this as GNU source so prototype for asprintf is define
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <sndfile.h>
#include <signal.h>

#define NUM_INPUT_PORTS 8		// Number of capture ports to connect to
#define RING_BUFFER_SIZE 16384		// Ring Buffer size in frames

int allConnected;					// Flag specifying all inputs and outputs are connected
jack_port_t **inputPortArray;				// Array of pointers to input ports
jack_default_audio_sample_t **inputBufferArray;		// Array of pointers to input buffers
jack_ringbuffer_t *ringBuffer;				// Ring buffer to pass audio data between JACK and main threads

int running;																// Flag to control disk writing process
pthread_mutex_t threadLock = PTHREAD_MUTEX_INITIALIZER;	// Mutex to control ring buffer access
pthread_cond_t dataReady = PTHREAD_COND_INITIALIZER;	// Condition to signal data is available in ring buffer

// This is called by the JACK server in a
// special realtime thread once for each audio cycle.
int jackProcess(jack_nframes_t nframes, void *arg)
{
	int portNum;
	jack_nframes_t frameNum;

	// Only proceed if all ports have been connected
	if (allConnected == 0) return 0;

	// Get pointers to the capture port buffers
	for (portNum=0 ; portNum<NUM_INPUT_PORTS ; portNum++)
	{
		inputBufferArray[portNum] = jack_port_get_buffer(inputPortArray[portNum], nframes);
	}

	// Iterate through input buffers, adding samples to the ring buffer
	for (frameNum=0; frameNum<nframes; frameNum++)
	{
		for (portNum=0; portNum<NUM_INPUT_PORTS; portNum++)
		{
			size_t written = jack_ringbuffer_write(ringBuffer, (void *) &inputBufferArray[portNum][frameNum], sizeof(jack_default_audio_sample_t));
			if (written != sizeof(jack_default_audio_sample_t))
			{
				printf("Ringbuffer overrun\n");
			}
		}
	}

	// Attempt to lock the threadLock mutex, returns zero if lock acquired
	if (pthread_mutex_trylock(&threadLock) == 0)
	{
		// Wake up thread which is waiting for condition (should only be called after lock acquired)
		pthread_cond_signal(&dataReady);

		// Unlock mutex
		pthread_mutex_unlock(&threadLock);
	}

	return 0;
}

// JACK calls this shutdown callback if the server ever shuts down
// or decides to disconnect the client
void jackShutdown(void *arg)
{
	printf("Jack shutdown\n");
	exit(1);
}

// Called when ctrl-c is pressed
void signalHandler(int sig)
{
	printf("Stopping\n");
	running = 0;
}


int main(void)
{
	printf("Wav Capture Port Test\n");

	int portNum;
	jack_status_t status;

	// Create the ring buffer
	ringBuffer = jack_ringbuffer_create(NUM_INPUT_PORTS * sizeof(jack_default_audio_sample_t) * RING_BUFFER_SIZE);

	// Initialise flags
	running = 0;			// File Writing not running
	allConnected = 0;		// Ports not connected

	// Open a client connection to the JACK server
	// The provided server name is null so the default server is selected
	jack_client_t *client = jack_client_open("WavCaptureTest", JackNullOption, &status, NULL);

	// Make sure the client was opened
	if (client == NULL) {
		fprintf(stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		if (status & JackServerFailed) fprintf(stderr, "Unable to connect to JACK server\n");
		exit (1);
	}

	// Tell the JACK server to call jackProcess when there is work to be done
	jack_set_process_callback(client, jackProcess, 0);

	// Tell the JACK server to call jackShutdown if it ever shuts down
	jack_on_shutdown(client, jackShutdown, 0);

	// Create an array of audio sample pointers
	// Each pointer points to the start of an audio buffer, one for each capture channel
	inputBufferArray = (jack_default_audio_sample_t**) malloc(NUM_INPUT_PORTS * sizeof(jack_default_audio_sample_t*));

	// Create an array of input ports
	inputPortArray = (jack_port_t**) malloc(NUM_INPUT_PORTS * sizeof(jack_port_t*));

	// Register each input port
	for (portNum=0 ; portNum<NUM_INPUT_PORTS ; portNum ++)
	{
		// Create port name
		char* portName;
		if (asprintf(&portName, "input%d", portNum) < 0) {
			fprintf(stderr, "Could not create portname for port %d", portNum);
			exit(1);
		}

		// Register the capture port
		inputPortArray[portNum] = jack_port_register(client, portName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		if (inputPortArray[portNum] == NULL) {
			fprintf(stderr, "Could not create input port %d\n", portNum);
			exit(1);
		}
	}

	// Tell the JACK server that we are ready. The jackProcess callback will now start running
	if (jack_activate(client)) {
		fprintf(stderr, "Could not activate client");
		exit(1);
	}

	// Get a list of capture ports
	const char **ports = jack_get_ports(client, "system:capture", NULL, JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "No physical capture ports\n");
		exit(1);
	}

	// List the found capture ports
	printf("Found capture ports:\n");
	for (portNum=0 ; ports[portNum]!=NULL ; portNum++) printf(" '%s'\n", ports[portNum]);

	// Connect the capture ports to the input ports
	for (portNum=0 ; portNum<NUM_INPUT_PORTS ; portNum ++)
	{
		if (ports[portNum] == NULL) {
			fprintf(stderr, "Not enough capture ports. Port num %d not found\n", portNum);
			exit(1);
		}

		if (jack_connect(client, ports[portNum], jack_port_name(inputPortArray[portNum]))) {
			fprintf(stderr, "Cannot connect input port\n");
			exit(1);
		}
	}

	// Free memory used for port names
	free(ports);

	// Calculate the number of bytes required to hold one frame of samples
	size_t bytesPerFrame = sizeof(jack_default_audio_sample_t) * NUM_INPUT_PORTS;

	// Create a buffer to hold a single frame
	jack_default_audio_sample_t *frameBuffer = (jack_default_audio_sample_t*) malloc(bytesPerFrame);

	// Acquire the mutex lock
	pthread_mutex_lock(&threadLock);

	SF_INFO info;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	info.channels = NUM_INPUT_PORTS;
	info.samplerate = 48000;

   SNDFILE *sndFile = sf_open("capture.wav", SFM_WRITE, &info);
	if (sndFile == NULL) {
		fprintf(stderr, "Error writing wav file: %s\n", sf_strerror(sndFile));
		exit(1);
	}

	// Set flags
	running = 1;		// Initialise running
	allConnected = 1;	// Flag that all ports are connected

	// Register the signal handling function to capture ctrl-c key press
	signal(SIGINT, signalHandler);

	do
	{
		// Block this thread until dataReady condition is signalled
		// The mutex is automatically unlocked whilst waiting
		// After signal received, mutex is automatically locked
		pthread_cond_wait(&dataReady, &threadLock);

		// Check if there is enough bytes for a whole frame
		while (jack_ringbuffer_read_space(ringBuffer) >= bytesPerFrame)
		{
			// Read a single frame of data
			jack_ringbuffer_read(ringBuffer, (void *)frameBuffer, bytesPerFrame);

			// Write data to file
		   sf_writef_float(sndFile, (float*)frameBuffer, 1);
		}
	}
	while (running);

	printf("Closing\n");
	jack_client_close(client);
   sf_write_sync(sndFile);
	sf_close(sndFile);
	jack_ringbuffer_free(ringBuffer);

	return 0;
}
