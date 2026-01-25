#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "glitch.h"

WindowManager wm = {0};

void handle_signal(int signal) {
	wm.running = 0;
	wm.restart = 1;
	printf("running: %d\n", wm.running);
	log_message(stderr, LOG_DEBUG, "Signal received: %d", signal);
}

static void* expose_timer_thread(void* arg) {
	(void)arg;

	for(;;) {
		sleep(1);

		if (wm.dpy != NULL) {
			XLockDisplay(wm.dpy);
			XEvent event = {0};
			event.type = Expose;
			event.xexpose.window = wm.root;
			event.xexpose.x = 0;
			event.xexpose.y = 0;
			event.xexpose.width = 1;
			event.xexpose.height = 1;
			event.xexpose.count = 0;

			// This is thread-safe - XSendEvent is designed for this.
			XSendEvent(wm.dpy, wm.root, False, ExposureMask, &event);
			XFlush(wm.dpy);
			XUnlockDisplay(wm.dpy);
		}
	}
	return NULL;
}

int main(int argc, char *argv[]) {
	(void)argc;
	set_log_level(get_log_level_from_env());

	// Initialize X11 threading support.
	if (!XInitThreads()) {
		log_message(stderr, LOG_ERROR, "XInitThreads failed");
		return 1;
	}

	init_window_manager();

	// Starts Expose ticker for updating widgets.
	pthread_t timer_tid;
	if (pthread_create(&timer_tid, NULL, expose_timer_thread, NULL) != 0) {
		log_message(stderr, LOG_ERROR, "failed to create timer thread");
	} else {
		pthread_detach(timer_tid);
	}

	// SIGUSR1 is used for restarting the window manager.
	// kill -s SIGUSR1 $(pidof glitch)
	signal(SIGUSR1, handle_signal);

	wm.running = 1;
	while(wm.running) {
		XNextEvent(wm.dpy, &wm.ev);

		switch (wm.ev.type) {
			case MapRequest:
				handle_map_request();
				break;
			case UnmapNotify:
				handle_unmap_notify();
				break;
			case DestroyNotify:
				handle_destroy_notify();
				break;
			case PropertyNotify:
				handle_property_notify();
				break;
			case MotionNotify:
				// Compress MotionNotify events.
				while (XCheckTypedEvent(wm.dpy, MotionNotify, &wm.ev));
				handle_motion_notify();
				break;
			case ClientMessage: 
				handle_client_message();
				break;
			case ButtonPress:
				handle_button_press();
				break;
			case ButtonRelease:
				handle_button_release();
				break;
			case KeyPress:
				handle_key_press();
				break;
			case KeyRelease:
				handle_key_release();
				break;
			case FocusIn:
				handle_focus_in();
				break;
			case FocusOut:
				handle_focus_out();
				break;
			case EnterNotify:
				handle_enter_notify();
				break;
			case Expose:
				handle_expose();
				break;
			case ConfigureRequest:
				handle_configure_request();
				break;
		}
	}

	deinit_window_manager();

	if (wm.restart) {
		execvp(argv[0], argv);
		perror("execvp");
	}

	return 0;
}
