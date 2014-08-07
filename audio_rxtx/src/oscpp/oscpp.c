#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <getopt.h>
#include "lo/lo.h"

//tb/140522/0807

/*
* 
* osc ping pong
* measure round trip with timetags
* compile:
* gcc -o oscrt oscpp.c `pkg-config --cflags --libs liblo` 
*
*/

lo_server_thread st;

void error(int num, const char *m, const char *path);
static void signal_handler(int sig);
//void sendKey(int key,char *ckey);
void send_ping();
void send_pong();

//default port to send OSC messages from (my port)
const char* localPort = "12777";
//default host to send OSC messages
const char* sendToHost = "127.0.0.1";
//default port to send OSC messages
const char* sendToPort = "12777";

//osc server
lo_server_thread st;
lo_address loa;

static double version=0.1;

long unsigned int ping_send_counter=0;
long unsigned int pong_send_counter=0;

long unsigned int ping_receive_counter=0;
long unsigned int pong_receive_counter=0;

long unsigned int ping_send_max=0;

double rt_sum=0;
double avg=0;
double diff=0;

double rt_time_max=0;

lo_timetag start_of_measurement;
lo_timetag end_of_measurement;

int shutdown_in_progress=0;

int interval_usec=1000;

int noping=0;

int ping_msg_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	send_pong(argv[0]->t);
	ping_receive_counter++;
	return 0;
}

int pong_msg_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{

	lo_timetag now;
	lo_timetag_now(&now);

	pong_receive_counter++;

	//Find the time difference between two timetags.
	//Returns a - b in seconds.

	diff=lo_timetag_diff(now,argv[0]->t);
	rt_sum+=diff;
	avg=(double)rt_sum/(double)ping_send_counter;

	if(diff>rt_time_max)
	{
		rt_time_max=diff;
	}

	return 0;
}

static void print_help (void)
{
	fprintf (stderr, "Usage: oscpp [Options] target_host\n");
	fprintf (stderr, "Options:\n");
	fprintf (stderr, "  Display this text:                     --help\n");
	fprintf (stderr, "  Local port:                    (%s) --lport  <integer>\n",localPort);
	fprintf (stderr, "  Remote port:                   (%s) --rport  <integer>\n",sendToPort);
	fprintf (stderr, "  Delay between messages [usec]:  (1000) --interval  <integer>\n");
	fprintf (stderr, "  Don't send pings (pong only):    (off) --noping\n");
	fprintf (stderr, "  Payload size [bytes]:              (0) --size  <integer>\n");

	fprintf (stderr, "  Limit number of sent messages:   (inf) --count  <integer>\n");
	fprintf (stderr, "target_host:   <string>\n");
	fprintf (stderr, "target_port:   <integer>\n\n");
	fprintf (stderr, "Example: oscpp --interval 100 10.10.10.3 1234\n");
	fprintf (stderr, "oscpp needs to run on the pinged host to get a reply.\n");
	fprintf (stderr, "See http://github.com/7890/jack_tools/\n\n");
	//needs manpage
	exit (1);
}

void print_header (char *prgname)
{
	fprintf (stderr, "\n%s v%.2f\n", prgname,version);
	fprintf (stderr, "(C) 2014 Thomas Brand  <tom@trellis.ch>\n");
}

int main (int argc, char *argv[])
{

	//handle ctrl+c etc.
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	// Make STDOUT unbuffered
	//setbuf(stdout, NULL);

	//command line options parsing
	//http://www.gnu.org/software/libc/manual/html_node/Using-Getopt.html
	static struct option long_options[] =
	{
		{"help",	no_argument,	    	0, 'h'},
		{"lport",       required_argument,      0, 'l'},
		{"rport",       required_argument,      0, 'r'},
		{"interval",    required_argument,      0, 'i'},
		{"size",        required_argument,     	0, 's'},
		{"count",       required_argument,      0, 'c'},
		{"noping",	no_argument,	&noping, 1},
		{0, 0, 0, 0}
	};

	//print program header
	print_header("oscpp - OSC Ping Pong");

	if (argc - optind < 1)
	{
		fprintf (stderr, "Missing arguments, see --help.\n\n");
		exit(1);
	}

	int opt;
	//do until command line options parsed
	while (1)
	{
		/* getopt_long stores the option index here. */
		int option_index = 0;

		opt = getopt_long (argc, argv, "", long_options, &option_index);

		/* Detect the end of the options. */
		if (opt == -1)
		{
			break;
		}
		switch (opt)
		{
			case 0:

			 /* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0)
			{
				break;
			}

			case 'h':
				print_help();
				break;

			case 'l':
				localPort=optarg;
				break;

			case 'r':
				sendToPort=optarg;
				break;

			case 'i':
				interval_usec=atoi(optarg);
				break;

			case 's':
				printf("not implemented\n");
				break;

			case 'c':
				ping_send_max=atoi(optarg);
				break;

			default:
				break;
		 } //end switch op
	}//end while(1)

	//remaining non optional parameter target host
	if(argc-optind != 1)
	{
		fprintf (stderr, "Wrong arguments, see --help.\n\n");
		exit(1);
	}

	sendToHost=argv[optind];
//	sendToPort=argv[++optind];

	//init osc
	st = lo_server_thread_new(localPort, error);
	lo_server_thread_add_method(st, "/pi", "t", ping_msg_handler, NULL);
	lo_server_thread_add_method(st, "/po", "t", pong_msg_handler, NULL);
	lo_server_thread_start(st);

	lo_timetag_now(&start_of_measurement);

	loa = lo_address_new(sendToHost, sendToPort);

	//fprintf(stderr, "ready\n");

	fprintf(stderr, "\nlegend\n\n");

	fprintf(stderr, "pis: # ping sent       por: # pong received\n");
	fprintf(stderr, "pir: # ping received   pos: # pong sent\n");
	fprintf(stderr, "cur: current roundtrip time (of last ping/pong)\n");
	fprintf(stderr, "max: max (peak) rt time   avg: average rt time\n");
	fprintf(stderr, "roundtrip times are indicted in milliseconds\n\n");

	//abort with ctrl+c -> signal_handler cleans up
	while ( 1==1 )
	{
		if(noping==0 && shutdown_in_progress==0)
		{
			send_ping();
		}

		fprintf(stderr,"\33[2K\rpis %lu por %lu pir %lu pos %lu cur %.3f max %.3f avg %.3f [ms]"
			,ping_send_counter
			,pong_receive_counter
			,ping_receive_counter
			,pong_send_counter
			,diff*1000
			,rt_time_max*1000
			,avg*1000);

		usleep(interval_usec);
	}
} //end main

//void sendKey(int key,char *ckey)
void send_ping()
{
	lo_message msg=lo_message_new();
	lo_timetag now;
	lo_timetag_now(&now);
	lo_message_add_timetag(msg,now);
	lo_send_message (loa, "/pi", msg);
	lo_message_free (msg);

	ping_send_counter++;

	//limit
	if(ping_send_max>0 && ping_send_counter>ping_send_max)
	{
		shutdown_in_progress=1;
	}
}

void send_pong(lo_timetag tt)
{
	lo_message msg=lo_message_new();
	lo_message_add_timetag(msg,tt);
	lo_send_message (loa, "/po", msg);
	lo_message_free (msg);
	pong_send_counter++;
}

//handle ctrl+c etc.
static void signal_handler(int sig)
{
	shutdown_in_progress=1;
	lo_timetag_now(&end_of_measurement);
	double start_end_diff=lo_timetag_diff(
		end_of_measurement,
		start_of_measurement
	);

	usleep(100000);

	fprintf(stderr,"\n\ntotal running time: %f seconds\n",start_end_diff);
	fprintf(stderr,"total sent ping messages:     %lu\n",ping_send_counter);
	fprintf(stderr,"total received pong messages: %lu\n",pong_receive_counter);

	fprintf(stderr,"pings sent per second: %.3f\n\n",ping_send_counter/start_end_diff);

	fprintf(stderr,"total received ping messages: %lu\n",ping_receive_counter);
	fprintf(stderr,"total sent pong messages:     %lu\n",pong_send_counter);

	fprintf(stderr,"pongs sent per second: %.3f\n\n",pong_send_counter/start_end_diff);

	exit(0);
}

//on osc error
void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d in path %s: %s\n", num, path, msg);
}
