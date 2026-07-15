#include <tvi.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>

static void sigint_handler(int signum) {
	(void)signum;
	tvi.interrupted = 1;
}

static void sigwinch_handler(int signum) {
	(void)signum;
	term_fetch_size();
	if (tvi.mode == MODE_VISUAL) {
		// TODO : go trough each windows
		tvi.first_window->width  = term_width;
		tvi.first_window->height = term_height - 1;
		render_all_windows(&tvi);
		render_prompt(&tvi);
		render_flush(&tvi);
	}
}
#endif

void signal_install_handlers(void) {
#ifdef HAVE_SIGACTION
	struct sigaction sa;
#ifdef HAVE_SIGEMPTYSET
	sigemptyset(&sa.sa_mask);
#endif
	sa.sa_flags = 0;

	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);

	sa.sa_handler = sigwinch_handler;
	sigaction(SIGWINCH, &sa, NULL);

#elif defined(HAVE_SIGNAL)
	signal(SIGINT, sigint_handler);
	signal(SIGWINCH, sigwinch_handler);
#endif
}
