#include <json/json.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "s_string.h"

static size_t write_func(char * ptr, size_t size, size_t nmemb, void * userdata) {
	struct String * str = (struct String *) userdata;
	if (str->data == NULL) {
		str->data = (char *)malloc(size * (nmemb + 1));
		str->length = 0;
		str->capacity = size * (nmemb + 1);
		if (str->data == NULL) {
			printf("Error in allocating String data memory");
			return 0;
		}
	}

	size_t new_length = str->length + size * (nmemb + 1);
	while (new_length > str->capacity) {
		if ((str->capacity *= 2) >= new_length) {
			char * new_data = (char *)realloc(str->data, str->capacity);
			if (!new_data) {
				printf("Error in reallocating String data memory");
				return 0;
			}
			str->data = new_data;
		}
	}

	memcpy(str->data + str->length, ptr, size * nmemb);
	memset(str->data + str->length + size * nmemb, 0, size);
	str->length += size * nmemb;
	return size * nmemb;
}

struct String * perform_curl(struct String * m, char * url, unsigned long port, unsigned long sessionId, struct json_object * config) {
	struct String * message = m;
	if (!message) {
		message = (struct String *)malloc(sizeof(struct String));
		if (!message) {
			printf("Error in allocating initial String memory");
			return NULL;
		}
		message->data = NULL;
	}
	if (message->data) {
		free(message->data);
		message->data = NULL;
	}

	CURL * curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	char * fullurl = url;
	curl_easy_setopt(curl, CURLOPT_PORT, port);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_func);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, message);

	if (config) {
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		const char * post_message = json_object_to_json_string(config);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_message);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(post_message));
	} else {
		fullurl = (char *)malloc(2 * strlen(url));
		snprintf(fullurl, 2 * strlen(url) -1, "%s?sessionId=%lu", url, sessionId);
	}
	curl_easy_setopt(curl, CURLOPT_URL, fullurl);

	CURLcode status = curl_easy_perform(curl);
	if (fullurl != url) {
		free(fullurl);
	}
	if (status) {
		printf("Error occurred in performing curl: %s\n", curl_easy_strerror(status));
		curl_easy_cleanup(curl);
		return NULL;
	}

	long int code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if (code != 200) {
		printf("Server returned error code: %ld\n", code);
		curl_easy_cleanup(curl);
		return NULL;
	}

	curl_easy_cleanup(curl);
	return message;
}

void delete_string(struct String * s) {
	if (s) {
		if (s->data) free(s->data);
		free(s);
	}
}
