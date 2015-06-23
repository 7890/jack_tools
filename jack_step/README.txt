jack_step - step JACK process cycles

-start a dummy jack with large client timeout:

$ jackd -t 100000 -d dummy

-start jack_step without arguments:

$ jack_step

jack_step has two modes: 'run' and 'step'.
the client will register with JACK and start processing in run mode.

-enter step mode with 'CTRL+c' -> JACK is in freewheeling mode now

-hit 'enter' to proceed to the next cycle(s)

(-do any JACK client or server actions, watch data in sisco etc.)

-leave step mode with 'c' followed by 'enter' -> JACK returns to normal non-freewheeling

-switch back and forth between run and step modes as many times as needed.

-quit program:
	while in run mode: from another terminal, kill -9 <PID of jack_step>
	while in step mode: 'CTRL+c' followed by 'enter'

tested in JACK versions 0.124.2 (aka 1) and 1.9.11 (aka 2)

to allow really long steps (i.e. pause for an unknown but limited time):
in JACK 2 codebase, in file common/JackConstants.h set higher timeout values:
#define FREEWHEEL_DRIVER_TIMEOUT 100000     // in sec
#define DRIVER_OPEN_TIMEOUT 5000       // in sec
