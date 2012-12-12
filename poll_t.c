#include <curl/curl.h>
#include <json/json.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include "s_string.h"
#include "poll_t.h"

#define REFRESH_RATE 500000000
#define PORT 80
#define POLL_CALL "http://api-sandbox.oanda.com/v1/instruments/poll.json"

unsigned long ID = 0;
int CLOCKID = -1;
pthread_mutex_t mID;
pthread_mutex_t mState;

State * STATE = NULL;
int state_ready = 0;
struct String * message = NULL;

State * getState(int clear) {
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

void copy_state(State * target, State * source) {
	if (target->num_instruments != source->num_instruments) {
		target->num_instruments = source->num_instruments;
		if (target->instruments) {
			free(target->instruments);
		}
		if (!(target->instruments = calloc(source->num_instruments, sizeof(Instrument_State)))) {
			target->num_instruments = 0;
			return;
		}
	}
	int i;
	for (i = 0; i < source->num_instruments; ++i) {
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
}

void delete_state(State * state, int delete_object) {
	if (state) {
		if (state->instruments) free(state->instruments);
		if (delete_object) free(state);
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

void setup_state(State * state, int argc, char ** argv) {
	pthread_mutex_lock(&mState);
	STATE = state;
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
	pthread_mutex_unlock(&mState);
}

void setup_instrument(const char * name, struct json_object * obj) {
	int i;
	for (i = 0; i < STATE->num_instruments; ++i) {
		Instrument_State * instrument = &STATE->instruments[i];
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
			return;
		}
	}
}

void clear_state_changed(State * state) {
	int i;
	for (i = 0; i < state->num_instruments; ++i) {
		state->instruments[i].changed = 0;
	}
}

//------------------------POLL-------------------

void send_poll_request(struct String * m, unsigned long id) {
	m = perform_curl(m, POLL_CALL, PORT, id, NULL);
	if (m->data) {
		struct json_object * poll_data = json_tokener_parse(m->data);
		if (!poll_data) {
			setID(0);
			printf("The response string could not be parsed: %s\n", m->data);
			return;
		}

		int i;
		struct json_object * price_array;
		if (json_object_object_get_ex(poll_data, "prices", &price_array) && json_object_get_array(price_array)) {
			pthread_mutex_lock(&mState);
			clear_state_changed(STATE);
			for (i = 0; i < json_object_array_length(price_array); ++i) {
				struct json_object * instrument = json_object_array_get_idx(price_array, i);
				struct json_object * name_obj;
				if (json_object_object_get_ex(instrument, "instrument", &name_obj)) {
					setup_instrument(json_object_get_string(name_obj), instrument);
				}
			}
			state_ready = 1;
			pthread_mutex_unlock(&mState);
		}
		json_object_put(poll_data);
	}
}

static void reset_clock(int clockid) {
	if (clockid < 0) return;
	struct itimerspec ts;
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = 0;
	ts.it_value.tv_nsec = REFRESH_RATE;
	timerfd_settime(clockid, 0, &ts, NULL);
}

void * poll_t(void * arg) {
	struct pollfd pfd;
	unsigned long id;
	while (id = getID()) {
		pfd.fd = CLOCKID;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 0)) {
			send_poll_request((struct String *)arg, id);
			reset_clock(CLOCKID);
		}
	}
	return NULL;
}

//----------------------"MAIN"-----------------
pthread_t setup_state_and_poll_thread(State * state, int argc, char ** argv) {
	pthread_mutex_init(&mState, NULL);
	setup_state(state, argc, argv);

	struct json_object * request_config = create_json_request(argc, argv);

	curl_global_init(CURL_GLOBAL_ALL);

	message = perform_curl(NULL, POLL_CALL, PORT, 0, request_config);
	json_object_put(request_config);
	if (!message) {
		printf("Could not obtain message, exiting\n");
		return 0;
	}

	unsigned long id = parse_setup_response(message->data);
	pthread_t thread = 0;

	if (!id) {
		printf("No ID was retrieved from the response\n");
	} else {
		pthread_mutex_init(&mID, NULL);
		setID(id);

		CLOCKID = timerfd_create(CLOCK_REALTIME, 0);
		reset_clock(CLOCKID);

		if (pthread_create(&thread, NULL, poll_t, message)) {
			thread = 0;
		}
	}
	return thread;
}

void close_poll_thread(pthread_t thread) {
	setID(0);
	if (thread) pthread_join(thread, NULL);
	pthread_mutex_destroy(&mID);
	pthread_mutex_destroy(&mState);
	if (CLOCKID >= 0) close(CLOCKID);
	delete_string(message);
	curl_global_cleanup();
}
