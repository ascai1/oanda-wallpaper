#include <curl/curl.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "poll_t.h"

#define SHOW_TEXT
#define FULLSCREEN
#define FONT_USED "-misc-fixed-bold-r-normal--15-140-75-75-c-90-iso10646-1"

#define T_BOUND 0.5
#define T_BOTTOM_BRIGHTNESS 0.3

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
	Display * dpy;
	Window w;
	GLXContext glx_context;
	int width, height;
	GLuint vHandle, fHandle, pHandle;
	Colormap cmap;
#ifdef SHOW_TEXT
	XFontStruct * font;
#endif
} wa;

struct {
	GLuint array_buffer;
	GLint position;
	GLint color;
	#ifdef SHOW_TEXT
	GLint font_base;
	short font_width;
	short font_height;
	#endif
} gla;

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

typedef struct {
	int x, y;
} Dimension;

Dimension get_grid_for_num_instruments(int num_instruments, int width, int height) {
	Dimension d = {0};
	if (!num_instruments) return d;

	float ratio = (float)width / height;
	d.x = round(sqrt(num_instruments * ratio));
	d.y = (num_instruments - 1) / d.x + 1;
	int i;
	for (i = (d.x > d.y ? d.y : d.x); i > sqrt(num_instruments * ratio / 2); --i) {
		if (num_instruments % i == 0) {
			if (width > height) {
				d.x = num_instruments / i;
				d.y = i;
			} else {
				d.x = i;
				d.y = num_instruments / i;
			}
			return d;
		}
	}
	return d;
}

//----------------------------DRAW---------------------------
void draw(Display * dpy, Window win, int s_width, int s_height) {
	refresh_state();

	glViewport(0, 0, s_width, s_height);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	Dimension d = get_grid_for_num_instruments(state.num_instruments, s_width, s_height);
	int i;
	for (i = 0; i < state.num_instruments; ++i) {
		float bottom = s_width / d.x * (i % d.x);
		float left = s_height / d.y * (d.y - 1 - i / d.x);
		float width = s_width / d.x;
		float height = s_height / d.y;
		glViewport(bottom, left, width, height);

		Instrument_State * is = &state.instruments[i];

		GLfloat brightness = pow(1 - pow((float)is->draw_state / MAX_DRAW_STATE * 2 - 1, 2), 0.5);
		if (is->draw_state) --is->draw_state;

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

		glBindBuffer(GL_ARRAY_BUFFER, gla.array_buffer);
		if (is->direction == 'u') {
			glBufferData(GL_ARRAY_BUFFER, sizeof(up), up, GL_STATIC_DRAW);
		} else {
			glBufferData(GL_ARRAY_BUFFER, sizeof(down), down, GL_STATIC_DRAW);
		}

		glEnableVertexAttribArray(gla.position);
		glVertexAttribPointer(gla.position, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);

		glEnableVertexAttribArray(gla.color);
		glVertexAttribPointer(gla.color, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));

		glDrawArrays(GL_TRIANGLES, 0, 3);

#ifdef SHOW_TEXT
		glListBase(gla.font_base);
		glColor4f(1.0, 1.0, 1.0, brightness / 2 + 0.5);
		float i_length = strlen(is->instrument);
		GLfloat i_left = -i_length * gla.font_width / width;
		GLfloat i_bottom = 0.9 - gla.font_height / height * 2;
		glRasterPos2f(i_left, i_bottom);
		glCallLists(i_length, GL_UNSIGNED_BYTE, (unsigned char *)is->instrument);

		char price[16] = {0};
		sprintf(price, "%f", is->price);
		float p_length = strlen(price);
		GLfloat p_left = -p_length * gla.font_width / width;
		GLfloat p_top = -0.9;
		glRasterPos2f(p_left, p_top);
		glCallLists(p_length, GL_UNSIGNED_BYTE, (unsigned char *)price);
#endif
	}

	glXSwapBuffers(dpy, win);
}

//--------------------------INITIALIZATION------------------
void initGL(Display * dpy, Window win, GLuint pHandle
#ifdef SHOW_TEXT
, XFontStruct * font
#endif
) {
	gla.position = glGetAttribLocation(pHandle, "position");
	gla.color = glGetAttribLocation(pHandle, "color");

#ifdef SHOW_TEXT
	int first = font->min_char_or_byte2;
	int last = font->max_char_or_byte2;
	gla.font_base = glGenLists(font->max_char_or_byte2 + 1);
	glXUseXFont(font->fid, first, last - first + 1, gla.font_base + first);

	gla.font_width = font->max_bounds.width;
	gla.font_height = font->max_bounds.ascent - font->max_bounds.descent;
#endif

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glGenBuffers(1, &gla.array_buffer);
}

int init_window() {
	wa.w = 0;
	wa.glx_context = NULL;
	wa.vHandle = wa.fHandle = wa.pHandle = 0;
	wa.cmap = 0;
#ifdef SHOW_TEXT
	wa.font = NULL;
#endif

	wa.dpy = XOpenDisplay(NULL);
	if (!wa.dpy) return 1;

	int attrList[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
	XVisualInfo * vinfo = glXChooseVisual(wa.dpy, DefaultScreen(wa.dpy), attrList);
	if (!vinfo) {
		printf("Unable to acquire visual\n");
		wa.dpy = NULL;
		return 1;
	}

	XSetWindowAttributes swa;
	swa.event_mask = ButtonPressMask | KeyPressMask | PointerMotionMask | StructureNotifyMask;
	swa.colormap = XCreateColormap(wa.dpy, RootWindow(wa.dpy, vinfo->screen), vinfo->visual, AllocNone);
	wa.cmap = swa.colormap;

#ifdef FULLSCREEN
	swa.override_redirect = 1;
#endif

	wa.w = XCreateWindow(wa.dpy,
			DefaultRootWindow(wa.dpy),
			0,
			0,
			DisplayWidth(wa.dpy, vinfo->screen),
			DisplayHeight(wa.dpy, vinfo->screen),
			0,
			0,
			CopyFromParent,
			vinfo->visual,
#ifdef FULLSCREEN
			CWColormap | CWEventMask | CWOverrideRedirect,
#else
			CWColormap | CWEventMask,
#endif
			&swa);
	if (!wa.w) {
		printf("Unable to create window\n");
		return 1;
	}

	wa.width = DisplayWidth(wa.dpy, vinfo->screen);
	wa.height = DisplayHeight(wa.dpy, vinfo->screen);

	wa.glx_context = glXCreateContext(wa.dpy, vinfo, NULL, True);
	XFree(vinfo);
	if (!wa.glx_context) {
		printf("Unable to create context\n");
		return 1;
	}
	if (!glXMakeCurrent(wa.dpy, wa.w, wa.glx_context)) {
		printf("glXMakeCurrent failed\n");
		return 1;
	}

	wa.vHandle = compileShader(vShader, GL_VERTEX_SHADER);
	wa.fHandle = compileShader(fShader, GL_FRAGMENT_SHADER);
	wa.pHandle = createProgram(wa.vHandle, wa.fHandle);
	if (!wa.vHandle || !wa.fHandle || !wa.pHandle) {
		printf("Compile failed\n");
		return 1;
	}
	glUseProgram(wa.pHandle);

#ifdef SHOW_TEXT
	wa.font = XLoadQueryFont(wa.dpy, FONT_USED);
	if (!wa.font) {
		printf("Font not found\n");
		return 1;
	}
#endif

	initGL(wa.dpy, wa.w, wa.pHandle
#ifdef SHOW_TEXT
	, wa.font
#endif
	);

	XMapRaised(wa.dpy, wa.w);

#ifdef FULLSCREEN
	XGrabKeyboard(wa.dpy, wa.w, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(wa.dpy, wa.w, True, PointerMotionMask, GrabModeAsync, GrabModeAsync, wa.w, None, CurrentTime);
#endif

	return 0;
}

void tear_down_window() {
	glDeleteShader(wa.vHandle);
	glDeleteShader(wa.fHandle);
	glDeleteProgram(wa.pHandle);

#ifdef SHOW_TEXT
	if (wa.font) XFreeFont(wa.dpy, wa.font);
#endif
	if (wa.cmap) XFreeColormap(wa.dpy, wa.cmap);
	if (wa.glx_context) {
		glXMakeCurrent(wa.dpy, None, NULL);
		glXDestroyContext(wa.dpy, wa.glx_context);
	}
	if (wa.w) XDestroyWindow(wa.dpy, wa.w);
	if (wa.dpy) XCloseDisplay(wa.dpy);
}

//-------------------------------MAIN-----------------------------
int main(int argc, char ** argv) {
	if (!(--argc)) {
		printf("You must specify at least one instrument to subscribe to (example format: EUR_USD)\n");
		return 1;
	}
	++argv;

	curl_global_init(CURL_GLOBAL_ALL);

	if (init_window()) {
		tear_down_window();
		curl_global_cleanup();
		return 1;
	}

	pthread_t poll_thread = setup_state_and_poll_thread(&state_buffer, argc, argv);
	state.num_instruments = 0;

	XEvent event;
	int done = 0;

	while (!done && poll_thread) {
		if (XPending(wa.dpy)) {
			XNextEvent(wa.dpy, &event);
			switch(event.type) {
			case ConfigureNotify: {
				XWindowAttributes xwa;
				XGetWindowAttributes(wa.dpy, wa.w, &xwa);
				wa.width = xwa.width;
				wa.height = xwa.height;
				break;
			}
			case ButtonPress:
			case KeyPress:
			case MotionNotify:
			{
				done = 1;
				break;
			}
			}
		}
		if (!done) {
			draw(wa.dpy, wa.w, wa.width, wa.height);
		}
	}

	close_poll_thread(poll_thread);
	tear_down_window();
	curl_global_cleanup();

	return 0;
}
