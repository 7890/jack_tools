/* firstNZsample.c - print time of first non-zero audio sample 
 *
 * (C) 2012 Robin Gareus <robin@gareus.org> 
 * GPLv2 https://www.gnu.org/licenses/gpl-2.0.html
 *
 * compile with:
 *   gcc -o firstNZsample firstNZsample.c -Wall -lsndfile
 */

//download from http://pastebin.com/qAxWNTTz
//23:25 <rgareus> http://pastebin.com/qAxWNTTz macht das in C
//Jun 10 2015

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sndfile.h>

int main (int argc, char **argv) {
	int c;
	long int s;
	char *filename;
  SF_INFO nfo;
  SNDFILE  *sndfile;
	float *buf;

	if (argc<2) {
		fprintf(stderr, "Usage: %s <audio-file>\n\n", argv[0]);
		fprintf(stderr, "this tool prints the time of the first non-zero audio-sample.\n\n");
		return -1;
	}
	filename = argv[1];

  memset(&nfo, 0, sizeof(SF_INFO));

  if ((sndfile = sf_open(filename, SFM_READ, &nfo)) == 0) {
    return -1;
	}
	if (!sndfile) {
		return -1;
	}

	buf = (float*) malloc(nfo.channels*sizeof(float));

	if (!buf) return -1;

//last sample
//23:30 <rgareus> Zeile  42   for (s = nfo.frames-1; s > 0; --s)

	for (s=0; s < nfo.frames; s++) {
		sf_readf_float(sndfile, buf, 1);
		for (c=0; c < nfo.channels; c++) {
			if (buf[c] != 0) {
				printf("First non-zero sample: %ld (chn:%d)\n", s, c+1);
				printf("Offset to 1sec in audio-samples %ld\n", nfo.samplerate-s);
				goto end;
			}
		}
	}
	printf("All samples are zero.\n");

end:
	sf_close(sndfile);
	free(buf);
	return 0;
}
