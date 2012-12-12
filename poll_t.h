#ifndef INST_STATE
#define INST_STATE

#include <pthread.h>

#define MAX_DRAW_STATE 100

typedef struct {
	char instrument[16];
	double price;
	char direction;
	int draw_state;
	char changed;
} Instrument_State;

typedef struct {
	int num_instruments;
	Instrument_State * instruments;
} State;

State * getState(int clear);
void copy_state(State * target, State * source);
pthread_t setup_state_and_poll_thread(State * state, int argc, char ** argv);
void close_poll_thread(pthread_t thread);

#endif
