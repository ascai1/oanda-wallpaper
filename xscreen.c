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
	int ncolors;
	XColor * colors;
	pthread_t poll_thread;
	State state;
	State state_buffer;
} window_config;

void refresh_state() {
	State * new_state = getState(1);
	if (new_state) {
		copy_state(&state, new_state);
		delete_state(new_state);
	}
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

ENTRYPOINT void reshape_oanda(ModeInfo *mi, int width, int height) { }

ENTRYPOINT Bool oanda_handle_event(ModeInfo *mi, XEvent *event) { }

ENTRYPOINT void release_oanda(ModeInfo *mi) {
	window_config * wc = &wcs[MI_SCREEN(mi)];

	close_poll_thread(wc->state, wc->thread);
}

ENTRYPOINT void init_oanda(ModeInfo *mi) {
	if (!wcs) {
		wcs = (window_config *) calloc(MI_NUM_SCREENS(mi), sizeof(window_config));
		if (!wcs) {
			printf("Could not allocate window config objects\n");
			exit(1);
		}
	}

	window_config * wc = &wcs[MI_SCREEN(mi)];

	wc->ncolors = 128;
	wc->colors = (XColor *)calloc(wc->ncolors, sizeof(XColor));
	make_smooth_colormap(MI_DISPLAY(mi), MI_VISUAL(mi), MI_COLORMAP(mi), wc->colors, &wc->ncolors, False, 0, False);

	wc->thread = setup_state_and_poll_thread(&wc->state_buffer, 
	wc->state.num_instruments = 0;
}
