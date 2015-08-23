
test loop (this should give a continuous tone):

	jack_playfile -v --loop -S 10000 a.wav

at different pitches (the numbers are overridden file sample rates):

	for i in 10000 20000 30000 40000 50000 60000 70000 80000 90000 100000; do jack_playfile -v -l -S$i a.wav; done

(there is still an issue of unclean end on ctrl+c or q)

playing half of the file, giving a continuous tone:

	jack_playfile --count 22050 --loop a.wav

short loops smaller than JACK period size are still problematic. they could be in-memory only (no seeking in file involved etc).
