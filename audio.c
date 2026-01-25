#include <pulse/pulseaudio.h>
#include <string.h>
#include "glitch.h"

extern WindowManager wm;

static void trigger_redraw(void) {
	if (!wm.dpy || wm.root == None) return;

	XLockDisplay(wm.dpy);
	XEvent ev = {0};
	ev.type = Expose;
	ev.xexpose.window = wm.root;
	ev.xexpose.x = 0;
	ev.xexpose.y = 0;
	ev.xexpose.width = 1;
	ev.xexpose.height = 1;
	ev.xexpose.count = 0;

	XSendEvent(wm.dpy, wm.root, False, ExposureMask, &ev);
	XFlush(wm.dpy);
	XUnlockDisplay(wm.dpy);
}

static void source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
	(void)c;
	(void)userdata;
	if (eol > 0 || !i) return;

	// Check if this is the default source or matches our criteria
	// For simplicity, we can just track the mute state of the default source
	// Pulseaudio usually calls this for the specific source we requested
	int muted = i->mute;
	if (wm.mic_muted != muted) {
		wm.mic_muted = muted;
		trigger_redraw();
	}
}

static void update_mic_state(pa_context *c) {
	pa_operation *o = pa_context_get_source_info_by_name(c, "@DEFAULT_SOURCE@", source_info_callback, NULL);
	if (o) pa_operation_unref(o);
}

static void subscribe_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
	(void)idx;
	(void)userdata;
	if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE) {
		update_mic_state(c);
	}
}

static void context_state_callback(pa_context *c, void *userdata) {
	(void)userdata;
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
			pa_context_set_subscribe_callback(c, subscribe_callback, NULL);
			pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SOURCE, NULL, NULL);
			update_mic_state(c);
			break;
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			break;
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;
	}
}

void init_audio(void) {
	wm.pa_mainloop = pa_threaded_mainloop_new();
	if (!wm.pa_mainloop) return;

	wm.pa_ctx = pa_context_new(pa_threaded_mainloop_get_api(wm.pa_mainloop), "glitch-wm");
	if (!wm.pa_ctx) return;

	pa_context_set_state_callback(wm.pa_ctx, context_state_callback, NULL);

	if (pa_context_connect(wm.pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		return;
	}

	if (pa_threaded_mainloop_start(wm.pa_mainloop) < 0) {
		return;
	}
}

void deinit_audio(void) {
	if (wm.pa_mainloop) pa_threaded_mainloop_stop(wm.pa_mainloop);
	if (wm.pa_ctx) pa_context_unref(wm.pa_ctx);
	if (wm.pa_mainloop) pa_threaded_mainloop_free(wm.pa_mainloop);
}

void toggle_mic_mute(const Arg *arg) {
	(void)arg;
	if (!wm.pa_ctx || pa_context_get_state(wm.pa_ctx) != PA_CONTEXT_READY) return;

	// Instant UI feedback
	wm.mic_muted = !wm.mic_muted;
	trigger_redraw();

	pa_threaded_mainloop_lock(wm.pa_mainloop);
	pa_operation *o = pa_context_set_source_mute_by_name(wm.pa_ctx, "@DEFAULT_SOURCE@", wm.mic_muted, NULL, NULL);
	if (o) pa_operation_unref(o);
	pa_threaded_mainloop_unlock(wm.pa_mainloop);
}
