#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "poll_t.h"

#define T_BOUND 0.5
#define T_BOTTOM_BRIGHTNESS 0.3
#define T_BORDER 2

static GLchar * vShader = "#version 120\n"
"attribute vec2 position;"
"attribute vec4 color;"
"varying vec4 vColor;"
"void main()"
"{"
	"gl_Position = vec4(position.x, position.y, 0.0, 1.0);"
	"vColor = vec4(color);"
"}\0";
 
static GLchar * fShader = "#version 120\n"
"varying vec4 vColor;"
"void main()"
"{"
	"gl_FragColor = vec4(vColor);"
"}\0";

struct {
	XWindowAttributes xwa;
	GLuint array_buffer;
	GLint position;
	GLint color;
	GLint font_base;
} attributes;

State state;
State state_buffer;

void refresh_state() {
	State * new_state = getState(1);
	if (new_state) {
		copy_state(&state, new_state);
		delete_state(new_state);
	}
}

GLuint compileShader(GLchar * shader, GLenum type) {
	GLuint ok;
	GLuint shaderID = glCreateShader(type);
	GLuint shaderLength = strlen(shader);
	glShaderSource(shaderID, 1, &shader, &shaderLength);
	glCompileShader(shaderID);
	glGetShaderiv(shaderID, GL_COMPILE_STATUS, &ok);
	if (ok) {
		return shaderID;
	}

	GLint length;
	glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &length);
	char * log = (char *)malloc(length);
	glGetShaderInfoLog(shaderID, length, NULL, log);
	printf("%s\n", log);
	free(log);
	return 0;
}

GLuint createProgram(GLuint vShader, GLuint fShader) {
	if (!vShader || !fShader) return 0;

	GLuint ok;
	GLuint programID = glCreateProgram();
	glAttachShader(programID, vShader);
	glAttachShader(programID, fShader);
	glLinkProgram(programID);
	glGetProgramiv(programID, GL_LINK_STATUS, &ok);
	if (ok) {
		return programID;
	}

	GLint length;
	glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &length);
	char * log = (char *)malloc(length);
	glGetProgramInfoLog(programID, length, NULL, log);
	printf("%s\n", log);
	free(log);
	return 0;
}

void initGL(Display * dpy, Window win, GLuint pHandle, XFontStruct * font) {
	attributes.position = glGetAttribLocation(pHandle, "position");
	attributes.color = glGetAttribLocation(pHandle, "color");

	int first = font->min_char_or_byte2;
	int last = font->max_char_or_byte2;
	attributes.font_base = glGenLists(font->max_char_or_byte2 + 1);
	glXUseXFont(font->fid, first, last - first + 1, attributes.font_base + first);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	XGetWindowAttributes(dpy, win, &attributes.xwa);
	glGenBuffers(1, &attributes.array_buffer);
}

typedef struct {
	int x, y;
} Dimension;

Dimension get_width_for_num_instruments(int num_instruments, int width, int height) {
	Dimension d = {0};
	if (!num_instruments) return d;
	d.x = round(sqrt(num_instruments));
	d.y = (num_instruments - 1) / d.x + 1;
	int i;
	for (i = d.x; i > pow(num_instruments, (float)1/3); --i) {
		if (num_instruments % i == 0) {
			d.x = i;
			d.y = num_instruments / i;
			break;
		}
	}
	if ((d.x > d.y && width < height) || (d.x < d.y && width > height)) {
		i = d.x;
		d.x = d.y;
		d.y = i;
	}
	return d;
}

void draw(Display * dpy, Window win) {
	refresh_state();

	glViewport(0, 0, attributes.xwa.width, attributes.xwa.height);
	glClearColor(1.0, 1.0, 1.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glViewport(0, 0, attributes.xwa.width - T_BORDER * 2, attributes.xwa.height - T_BORDER * 2);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	Dimension d = get_width_for_num_instruments(state.num_instruments, attributes.xwa.width, attributes.xwa.height);
	int i;
	for (i = 0; i < state.num_instruments; ++i) {
		float bottom = (attributes.xwa.width - T_BORDER * 2) / d.x * (i % d.x) + T_BORDER;
		float left = (attributes.xwa.height - T_BORDER * 2) / d.y * (d.y - 1 - i / d.x) + T_BORDER;
		float width = (attributes.xwa.width - T_BORDER * 2) / d.x;
		float height = (attributes.xwa.height - T_BORDER * 2) / d.y;
		glViewport(bottom, left, width, height);

		Instrument_State * is = &state.instruments[i];

		GLfloat brightness = pow(1 - pow((float)is->draw_state / MAX_DRAW_STATE * 2 - 1, 2), 0.25);
		if (is->draw_state) --is->draw_state;
		if (strcmp(is->instrument, "EUR_USD") == 0) printf("%d\n", is->draw_state);

		GLfloat up[] = {
			-T_BOUND, -T_BOUND, 0.0, T_BOTTOM_BRIGHTNESS, 0.0, brightness,
			0.0, T_BOUND, 0.0, 1.0, 0.0, brightness,
			T_BOUND, -T_BOUND, 0.0, T_BOTTOM_BRIGHTNESS, 0.0, brightness
		};
		GLfloat down[] = {
			-T_BOUND, T_BOUND, T_BOTTOM_BRIGHTNESS, 0.0, 0.0, brightness,
			0.0, -T_BOUND, 1.0, 0.0, 0.0, brightness,
			T_BOUND, T_BOUND, T_BOTTOM_BRIGHTNESS, 0.0, 0.0, brightness
		};

		glBindBuffer(GL_ARRAY_BUFFER, attributes.array_buffer);
		if (is->direction == 'u') {
			glBufferData(GL_ARRAY_BUFFER, sizeof(up), up, GL_STATIC_DRAW);
		} else {
			glBufferData(GL_ARRAY_BUFFER, sizeof(down), down, GL_STATIC_DRAW);
		}

		glEnableVertexAttribArray(attributes.position);
		glVertexAttribPointer(attributes.position, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);

		glEnableVertexAttribArray(attributes.color);
		glVertexAttribPointer(attributes.color, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));

		glDrawArrays(GL_TRIANGLES, 0, 3);

/*
		glListBase(attributes.font_base);
		glColor4f(1.0, 1.0, 1.0, brightness);
		glRasterPos2f(-0.3, 0.8);
		glCallLists(strlen(is->instrument), GL_UNSIGNED_BYTE, (unsigned char *)is->instrument);

		char price[16] = {0};
		sprintf(price, "%f", is->price);
		glRasterPos2f(-0.3, -1.0);
		glCallLists(strlen(price), GL_UNSIGNED_BYTE, (unsigned char *)price);
		*/
	}

	glFlush();
	glXSwapBuffers(dpy, win);
}

int main(int argc, char ** argv) {
	if (!(--argc)) {
		printf("You must specify at least one instrument to subscribe to (example format: EUR_USD)\n");
		return 1;
	}
	++argv;

	XEvent event;
	int done = 0;


	Display * dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;

	int attrList[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
	XVisualInfo * vinfo = glXChooseVisual(dpy, DefaultScreen(dpy), attrList);
	if (!vinfo) {
		printf("Unable to acquire visual\n");
		return 1;
	}

	XSetWindowAttributes swa;
	swa.event_mask = ButtonPressMask | KeyPressMask;
	swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vinfo->screen), vinfo->visual, AllocNone);

	Window w = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, DisplayWidth(dpy, vinfo->screen)/2, DisplayHeight(dpy, vinfo->screen)/2, 0, 0, CopyFromParent, vinfo->visual, CWColormap | CWEventMask, &swa);

	GLXContext glx = glXCreateContext(dpy, vinfo, NULL, True);
	if (glx == NULL) {
		printf("Unable to create context\n");
		return 1;
	}
	if (!glXMakeCurrent(dpy, w, glx)) {
		printf("glXMakeCurrent failed\n");
		return 1;
	}

	GLuint vHandle = compileShader(vShader, GL_VERTEX_SHADER);
	GLuint fHandle = compileShader(fShader, GL_FRAGMENT_SHADER);
	GLuint pHandle = createProgram(vHandle, fHandle);
	if (!vHandle || !fHandle || !pHandle) {
		glDeleteShader(vHandle);
		glDeleteShader(fHandle);
		glDeleteProgram(pHandle);
		printf("Compile failed\n");
		return;
	}
	glUseProgram(pHandle);

	XFontStruct * font = XLoadQueryFont(dpy, "-misc-fixed-bold-r-normal--15-140-75-75-c-90-iso10646-1");
	if (!font) {
		printf("Font not found\n");
		return 1;
	}

	initGL(dpy, w, pHandle, font);

	XMapRaised(dpy, w);

	pthread_t poll_thread = setup_state_and_poll_thread(&state_buffer, argc, argv);
	state.num_instruments = 0;

	while (!done && poll_thread) {
		if (XPending(dpy)) {
			XNextEvent(dpy, &event);
			switch(event.type) {
			case ButtonPress: done = 1; break;
			}
		}
		draw(dpy, w);
	}

	close_poll_thread(poll_thread);
	glDeleteShader(vHandle);
	glDeleteShader(fHandle);
	glDeleteProgram(pHandle);

	XFreeFont(dpy, font);
	XFreeColormap(dpy, swa.colormap);
	glXMakeCurrent(dpy, None, NULL);
	glXDestroyContext(dpy, glx);
	XDestroyWindow(dpy, w);
	XFree(vinfo);
	XCloseDisplay(dpy);
	return 0;
}
