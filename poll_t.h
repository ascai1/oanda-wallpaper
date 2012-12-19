#ifndef INST_STATE
#define INST_STATE

#include <pthread.h>
#include "s_string.h"

#define MAX_DRAW_STATE 60

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
	pthread_mutex_t mState;
	int ready;
	struct String * message;
	int clockid;
} State;

//State * getState(int clear);
State * new_state();
void delete_state(State * state);
void lock_state(State * state);
void unlock_state(State * state);
int is_ready(State * state);
void copy_state(State * target, State * source);
pthread_t setup_state_and_poll_thread(State * state, int argc, char ** argv);
void destroy_state_and_poll_thread(State * state, pthread_t thread);

#endif
