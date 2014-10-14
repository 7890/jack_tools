#include <stdio.h>
#include "lo/lo.h"

//test for MAX_UDP_MSG_SIZE
//if available, return 0 else 1

int print_lo_info()
{
	int major, minor, lt_maj, lt_min, lt_bug;
	char extra[256];
	char string[256];

	lo_version(string, 256,
			   &major, &minor, extra, 256,
			   &lt_maj, &lt_min, &lt_bug);

	printf("liblo version string `%s'\n", string);
	printf("liblo version: %d.%d%s\n", major, minor, extra);
	printf("liblo libtool version: %d.%d.%d\n", lt_maj, lt_min, lt_bug);

	printf("liblo MAX_MSG_SIZE: %d\n", LO_MAX_MSG_SIZE);
#ifdef LO_MAX_UDP_MSG_SIZE
	printf("liblo MAX_UDP_MSG_SIZE: %d\n", LO_MAX_UDP_MSG_SIZE);
	printf("liblo LO_DEFAULT_MAX_MSG_SIZE: %d\n", LO_DEFAULT_MAX_MSG_SIZE);
	printf("\n");
	return 0;
#endif

	printf("\n");
	return 1;
}

int main(int argc, char *argv[])
{
	return print_lo_info();
}
