#include "fetch.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char ** argv) {
	--argc;
	++argv;
	State state;
	pthread_t thread = setup_state_and_poll_thread(&state, argc, argv);
	int count = 0;
	while (count != 5 && thread) {
		State * state_p;
		if (state_p = getState(1)) {
			int i;
			for (i = 0; i < state_p->num_instruments; ++i) {
				Instrument_State * instrument = &state_p->instruments[i];
				printf("%s %f %c\n", instrument->instrument, instrument->price, instrument->direction);
			}
			delete_state(state_p, 1);
			++count;
		}
	}
	close_poll_thread(thread);
	delete_state(&state, 0);
}
