#include <curl/curl.h>
#include <json/json.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include "poll_t.h"

#define REFRESH_RATE 500000000
#define PORT 80
#define POLL_CALL "http://api-sandbox.oanda.com/v1/instruments/poll.json"

unsigned long ID = 0;
pthread_mutex_t mID;

/*
State * getState() {
	if (!STATE) return NULL;
	if (state_ready) {
		state_ready = !clear;
		State * newState = malloc(sizeof(State));
		if (!newState) return NULL;
		newState->num_instruments = 0;
		newState->instruments = NULL;

		pthread_mutex_lock(&mState);
		copy_state(newState, STATE);
		pthread_mutex_unlock(&mState);

		return newState;
	} else {
		return NULL;
	}
}
*/
State * new_state() {
	State * state = (State *) malloc(sizeof(State));
	if (!state) return NULL;

	state->num_instruments = 0;
	state->instruments = NULL;
	pthread_mutex_init(&state->mState, NULL);
	state->ready = 0;
	state->message = NULL;
	state->clockid = -1;
	return state;
}

void lock_state(State * state) {
	if (!state) return;
	pthread_mutex_lock(&state->mState);
}
void unlock_state(State * state) {
	if (!state) return;
	pthread_mutex_unlock(&state->mState);
}

int is_ready(State * state) {
	pthread_mutex_lock(&state->mState);
	int r = state->ready;
	pthread_mutex_unlock(&state->mState);
	return r;
}

void copy_state(State * target, State * source) {
	if (source == NULL || target == NULL || source == target) return;

	pthread_mutex_lock(&source->mState);
	pthread_mutex_lock(&target->mState);

	target->ready = source->ready;
	if (source->ready <= 0) {
		pthread_mutex_unlock(&source->mState);
		pthread_mutex_unlock(&target->mState);
		return;
	}

	target->message = source->message;
	target->clockid = source->clockid;

	if (target->num_instruments != source->num_instruments) {
		target->num_instruments = source->num_instruments;
		if (target->instruments) {
			free(target->instruments);
		}
		if (!(target->instruments = calloc(source->num_instruments, sizeof(Instrument_State)))) {
			target->num_instruments = 0;
		}
	}

	int i;
	for (i = 0; i < target->num_instruments; ++i) {
		Instrument_State * source_i = &source->instruments[i];
		Instrument_State * target_i = &target->instruments[i];
		strcpy(target_i->instrument, source_i->instrument);
		target_i->price = source_i->price;
		target_i->direction = source_i->direction;
		if (target_i->changed = source_i->changed) {
			if (target_i->draw_state) {
				target_i->draw_state = MAX_DRAW_STATE * 4 / 5;
			} else {
				target_i->draw_state = source_i->draw_state;
			}
		}
	}

	pthread_mutex_unlock(&source->mState);
	pthread_mutex_unlock(&target->mState);
}

void delete_state(State * state) {
	if (state) {
		if (state->instruments) free(state->instruments);
		if (state->message) delete_string(state->message);
		pthread_mutex_destroy(&state->mState);
		if (state->clockid >= 0) close(state->clockid);
		free(state);
	}
}

unsigned long getID() {
	unsigned long id;
	pthread_mutex_lock(&mID);
	id = ID;
	pthread_mutex_unlock(&mID);
	return id;
}

void setID(unsigned long id) {
	pthread_mutex_lock(&mID);
	ID = id;
	pthread_mutex_unlock(&mID);
}

void reset_clock(int clockid) {
	if (clockid < 0) return;
	struct itimerspec ts;
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = 0;
	ts.it_value.tv_nsec = REFRESH_RATE;
	timerfd_settime(clockid, 0, &ts, NULL);
}

//-------------------------SETUP-----------------------
struct json_object * create_json_request(int argc, char ** argv) {
	struct json_object * conf_prices = json_object_new_array();
	int i;
	for (i = 0; i < argc; ++i) {
		json_object_array_add(conf_prices, json_object_new_string(argv[i]));
	}

	struct json_object * request_config = json_object_new_object();
	json_object_object_add(request_config, "prices", conf_prices);
	return request_config;
}

unsigned long parse_setup_response(char * response) {
	struct json_object * result = json_tokener_parse(response);
	struct json_object * id_obj;
	unsigned long id = 0;
	if (result == NULL || !json_object_object_get_ex(result, "sessionId", &id_obj)) {
		printf("Something went wrong in processing the string %s\n", response);
		if (result) json_object_put(result);
	} else {
		id = json_object_get_int64(id_obj);
		json_object_put(result);
	}
	return id;
}

//-------------------------STATE MANIPULATION----------------

void setup_state(State * state, int argc, char ** argv, struct String * message) {
	pthread_mutex_init(&state->mState, NULL);
	pthread_mutex_lock(&state->mState);
	state->ready = 0;
	state->message = message;
	state->num_instruments = argc;
	state->instruments = calloc(argc, sizeof(Instrument_State));
	int i;
	for (i = 0; i < argc; ++i) {
		Instrument_State * instrument = &state->instruments[i];
		strcpy(instrument->instrument, *argv++);
		instrument->price = 0;
		instrument->draw_state = 0;
		instrument->changed = 0;
	}
	state->clockid = timerfd_create(CLOCK_REALTIME, 0);
	reset_clock(state->clockid);
	pthread_mutex_unlock(&state->mState);
}

void setup_instrument(State * state, const char * name, struct json_object * obj) {
	pthread_mutex_lock(&state->mState);
	int i;
	for (i = 0; i < state->num_instruments; ++i) {
		Instrument_State * instrument = &state->instruments[i];
		if (strcmp(instrument->instrument, name) == 0) {
			struct json_object * bid_price = NULL;
			json_object_object_get_ex(obj, "bid", &bid_price);
			struct json_object * ask_price = NULL;
			json_object_object_get_ex(obj, "ask", &ask_price);

			if (bid_price && ask_price) {
				double price = (json_object_get_double(ask_price) + json_object_get_double(bid_price)) / 2;
				if (price > instrument->price) {
					instrument->direction = 'u';
				} else {
					instrument->direction = 'd';
				}
				instrument->price = price;
				instrument->draw_state = MAX_DRAW_STATE;
				instrument->changed = 1;
			}
			break;
		}
	}
	pthread_mutex_unlock(&state->mState);
}

void clear_state_changed(State * state) {
	pthread_mutex_lock(&state->mState);
	int i;
	for (i = 0; i < state->num_instruments; ++i) {
		state->instruments[i].changed = 0;
	}
	pthread_mutex_unlock(&state->mState);
}

//------------------------POLL-------------------

void send_poll_request(State * state, unsigned long id) {
	pthread_mutex_lock(&state->mState);
	state->message = perform_curl(state->message, POLL_CALL, PORT, id, NULL);
	if (!state->message->data) {
		printf("Poll request got null response\n");
		return;
	}
	struct json_object * poll_data = json_tokener_parse(state->message->data);
	if (!poll_data) {
		printf("The response string could not be parsed: %s\n", state->message->data);
		return;
	}
	pthread_mutex_unlock(&state->mState);

	int i;
	struct json_object * price_array;
	if (json_object_object_get_ex(poll_data, "prices", &price_array) && json_object_get_array(price_array)) {
		clear_state_changed(state);
		pthread_mutex_lock(&state->mState);
		for (i = 0; i < json_object_array_length(price_array); ++i) {
			struct json_object * instrument = json_object_array_get_idx(price_array, i);
			struct json_object * name_obj;
			if (json_object_object_get_ex(instrument, "instrument", &name_obj)) {
				setup_instrument(state, json_object_get_string(name_obj), instrument);
			}
		}
		state->ready = 1;
		pthread_mutex_unlock(&state->mState);
	}
	json_object_put(poll_data);
}

void * poll_t(void * arg) {
	struct pollfd pfd;
	int ready;
	State * state = (State *)arg;
	while ((ready = is_ready(state)) >= 0) {
		pfd.fd = state->clockid;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 0)) {
			send_poll_request(state, getID());
			reset_clock(state->clockid);
		}
	}
	return NULL;
}

//----------------------"MAIN"-----------------
pthread_t setup_state_and_poll_thread(State * state, int argc, char ** argv) {
	if (!state) return 0;

	unsigned long id = ID;
	if (id) {
		setup_state(state, argc, argv, NULL);
	} else {
		struct json_object * request_config = create_json_request(argc, argv);

		struct String * message = perform_curl(NULL, POLL_CALL, PORT, 0, request_config);
		json_object_put(request_config);

		setup_state(state, argc, argv, message);

		if (!message) {
			printf("Could not obtain message, exiting\n");
			return 0;
		}

		id = parse_setup_response(message->data);

		pthread_mutex_init(&mID, NULL);
		setID(id);
	}

	pthread_t thread = 0;

	if (!id) {
		printf("No ID was retrieved from the response\n");
	} else {
		if (pthread_create(&thread, NULL, poll_t, state)) {
			thread = 0;
		}
	}
	return thread;
}

void destroy_state_and_poll_thread(State * state, pthread_t thread) {
	pthread_mutex_lock(&state->mState);
	state->ready = -1;
	pthread_mutex_unlock(&state->mState);
	if (thread) pthread_join(thread, NULL);
	delete_state(state);
}
