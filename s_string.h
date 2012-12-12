#ifndef S_STRING
#define S_STRING

struct String {
	char * data;
	size_t length;
	size_t capacity;
};

void delete_string(struct String * s);

struct String * perform_curl(struct String * m, char * url, unsigned long port, unsigned long sessionId, struct json_object * config);

#endif
