#include <curl/curl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include "xlockmore.h"
#include "poll_t.h"

#define FONT_USED "-misc-fixed-bold-r-normal--15-140-75-75-c-90-iso10646-1"

#define T_BOUND 0.5
#define T_BOTTOM_BRIGHTNESS 0.3

static char * all_instruments[] = {
	"AUD_CAD",
	"AUD_CHF",
	"AUD_JPY",
	"AUD_NZD",
	"AUD_SGD",
	"AUD_USD",
	"CAD_CHF",
	"CAD_JPY",
	"CHF_JPY",
	"EUR_AUD",
	"EUR_CAD",
	"EUR_CHF",
	"EUR_GBP",
	"EUR_JPY",
	"EUR_NZD",
	"EUR_PLN",
	"EUR_SGD",
	"EUR_USD",
	"GBP_AUD",
	"GBP_CAD",
	"GBP_CHF",
	"GBP_JPY",
	"GBP_NZD",
	"GBP_USD",
	"NZD_CAD",
	"NZD_CHF",
	"NZD_JPY",
	"NZD_USD",
	"USD_CAD",
	"USD_CHF",
	"USD_DKK",
	"USD_JPY",
	"USD_NOK",
	"USD_SEK",
	"USD_SGD",
	"USD_ZAR",
	"XAG_JPY",
	"XAG_USD",
	"XAU_JPY",
	"XAU_USD"
};

typedef struct {
	GLXContext * glx_context;
	int ncolors;
	XColor * colors;
	pthread_t poll_thread;
	State * state, state_buffer;
	XFontStruct * font;
	GLint font_base;
	short font_width, font_height;
} window_config;

static window_config * wcs;

typedef struct {
	int x, y;
} Dimension;

static Dimension get_grid_for_num_instruments(int num_instruments, int width, int height) {
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

ENTRYPOINT void reshape_oanda(ModeInfo *mi, int width, int height) {
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glClear(GL_COLOR_BUFFER_BIT);
}

ENTRYPOINT Bool oanda_handle_event(ModeInfo *mi, XEvent *event) { }

ENTRYPOINT void release_oanda(ModeInfo *mi) {
	int screen;
	for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++) {
		window_config * wc = &wcs[screen];

		destroy_state_and_poll_thread(wc->state_buffer, wc->thread);
		delete_state(wc->state);
		free_colors(MI_DISPLAY(mi), MI_COLORMAP(mi), wc->colors, wc->ncolors);
		free(wc->colors);
		glXMakeCurrent(MI_DISPLAY(mi), None, NULL);
		glXDestroyContext(MI_DISPLAY(mi), *(wc->glx_context));
	}
	free(wcs);
	curl_global_cleanup();
}

ENTRYPOINT void init_oanda(ModeInfo *mi) {
	if (!wcs) {
		wcs = (window_config *) calloc(MI_NUM_SCREENS(mi), sizeof(window_config));
		if (!wcs) {
			printf("Could not allocate window config objects\n");
			exit(1);
		}
	}

	curl_global_init(CURL_GLOBAL_ALL);

	window_config * wc = &wcs[MI_SCREEN(mi)];

	if (wc->glx_context = init_GL(mi)) {
		reshape_oanda(mi, MI_WIDTH(mi), MI_HEIGHT(mi));
	}

	wc->ncolors = 128;
	wc->colors = (XColor *)calloc(wc->ncolors, sizeof(XColor));
	make_smooth_colormap(MI_DISPLAY(mi), MI_VISUAL(mi), MI_COLORMAP(mi), wc->colors, &wc->ncolors, False, 0, False);

	wc->state = new_state();
	wc->state_buffer = new_state();
	wc->thread = setup_state_and_poll_thread(wc->state_buffer, sizeof(all_instruments) / sizeof(char *), all_instruments);
	wc->font = XLoadQueryFont(MI_DISPLAY(mi), FONT_USED);
	if (wc->font) {
	    int first = wc->font->min_char_or_byte2;
		int last = wc->font->max_char_or_byte2;
		wc->font_base = glGenLists(last + 1);
		xscreensaver_glXUseXFont(MI_DISPLAY(mi), wc->font->fid, first, last - first + 1, wc->font_base + first);
		wc->font_width = font->max_bounds.width;
		wc->font_height = font->max_bounds.ascent - font->max_bounds.descent;
	} else {
		wc->font_base = 0;
		wc->font_width = 0;
		wc->font_height = 0;
		printf("Could not load font %s\n", FONT_USED);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

ENTRYPOINT void draw_oanda(ModeInfo *mi) {
	window_config * wc = &wcs[MI_SCREEN(mi)];

	if (!wc->glx_context) return;
	glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(wc->glx_context));

	copy_state(wc->state, wc->state_buffer, 1);

	State * state = wc->state;
	lock_state(state);

	Dimension d = get_grid_for_num_instruments(state->num_instruments, MI_WIDTH(mi), MI_HEIGHT(mi));
	int i;
	for (i = 0; i < state->num_instruments; ++i) {
		float width = MI_WIDTH(mi) / d.x;
		float height = MI_HEIGHT(mi) / d.y;
		float left = width * (i % d.x);
		float bottom = height * (d.y - 1 - i / d.x);
		glViewport(left, bottom, width, height);

		Instrument_State * is = &state->instruments[i];

		GLfloat brightness = pow(1 - pow((float)is->draw_state / MAX_DRAW_STATE * 2 - 1, 2), 0.5);
		if (is->draw_state) -- is->draw_state;

		int factor = (is->direction == 'u' ? 1 : -1);

		glBegin(GL_TRIANGLE);
		glColor4f(0.0, T_BOTTOM_BRIGHTNESS, 0.0, brightness);
		glVertex2f(-T_BOUND, T_BOUND * factor);
		glColor4f(0.0, 1.0, 0.0, brightness);
		glVertex2f(0, -T_BOUND * factor);
		glColor4f(0.0, T_BOTTOM_BRIGHTNESS, 0.0, brightness);
		glVertex2f(T_BOUND, T_BOUND * factor);
		glEnd();

		if (wc->font_base) {
			glListBase(wc->font_base);
			glColor4f(0.0, 1.0, 0.0, brightness / 2 + 0.5);
			GLfloat i_left = -i_length * wc->font_width / width;
			GLfloat i_bottom = 0.9 - wc->font_height / height * 2;
			glRasterPos2f(i_left, i_bottom);
			glCallLists(i_length, GL_UNSIGNED_BYTE, (unsigned char *)is->instrument);

			char price[16] = {0};
			sprintf(price, "%f", is->price);
			float p_length = strlen(price);
			GLfloat p_left = -p_length * wc->font_width / width;
			GLfloat p_bottom = -0.9;
			glRasterPos2f(p_left, p_bottom);
			glCallLists(p_length, GL_UNSIGNED_BYTE, (unsigned char *)price);
		}
	}
	unlock_state(state);
}

XSCREENSAVER_MODULE_1("OANDA", oanda);
