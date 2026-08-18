#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#include <errno.h>
#include <tvi.h>

#ifdef HAVE_TERMIOS_H
static struct termios old;
static struct winsize winsz;
#endif
int term_width;
int term_height;
static cell_t *old_buffer;
static cell_t *new_buffer;
static int cursor_x;
static int cursor_y;

#define cell(buf, x, y) &buf[(x) + term_width * (y)]
#define line(buf, y) cell(buf, 0, y)

static const char *codes[TERM_CODES_COUNT] = {
	[TERM_GOTO]           = ESC"[%d;%df",
	[TERM_COLOR_RESET]    = ESC"[0m",
	[TERM_COLOR_INVERSE]  = ESC"[7m",
	[TERM_COLOR_SET_FG]   = ESC"[3%dm",
	[TERM_COLOR_SET_BG]   = ESC"[4%dm",
	[TERM_CLEAR_END_LINE] = ESC"[K",
	[TERM_INSERT]         = ESC"[%d@",
	[TERM_DELETE]         = ESC"[%dP",
	[TERM_INSERT_LINE]    = ESC"[%dL",
	[TERM_DELETE_LINE]    = ESC"[%dM",
	[TERM_CLEAR]          = ESC"[2J",
};

const char *term_get_code(int code) {
	return codes[code];
}

size_t term_get_code_len(int code) {
	const char *str = term_get_code(code);
	if (!str) return 0;
	return strlen(str);
}

void term_send_code(int code, ...) {
	const char *fmt = term_get_code(code);
	if (!fmt) return;
	va_list args;
	va_start(args, code);
	vprintf(fmt, args);
	va_end(args);
}

void term_fetch_size(void) {
	int old_term_width  = term_width;
	int old_term_height = term_height;
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCGWINSZ) && defined(STDOUT_FILENO)
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz) != 0 || winsz.ws_col <= 0 || winsz.ws_row <= 0) {
		if (ioctl(STDIN_FILENO, TIOCGWINSZ, &winsz) != 0 || winsz.ws_col <= 0 || winsz.ws_row <= 0) {
			winsz.ws_col = 80;
			winsz.ws_row = 25;
		}
	}
	term_width = winsz.ws_col;
	term_height = winsz.ws_row;
#else
	// just blindly guess terminal size
	term_width = 80;
	term_height = 25;
#endif
	if (term_width != old_term_width || term_height != old_term_height) {
		// reallocate buffers
		free(old_buffer);
		free(new_buffer);
		old_buffer = malloc(term_width * term_height * sizeof(cell_t));
		new_buffer = malloc(term_width * term_height * sizeof(cell_t));
		for (int i=0; i<term_width * term_height; i++) {
			old_buffer[i].attr = 0;
			old_buffer[i].c = ' ';
			new_buffer[i].attr = 0;
			new_buffer[i].c = ' ';
		}
		term_send_code(TERM_CLEAR);
	}
}

int term_enable_raw_mode(void) {
#ifdef HAVE_TERMIOS_H // without termios it will be probably broken but we can try
	// save old termios
	if(tcgetattr(STDIN_FILENO, &old) < 0){
		perror("tcgetattr");
		return -1;
	}
	struct termios new = old;
	new.c_lflag &= ~(ICANON | ECHO);
	if(tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0){
		perror("tcsetattr");
		return -1;
	}
#endif

	// disable buffering
	setvbuf(stdin, NULL, _IONBF, 0);;
	return 0;
}

void term_quit_raw_mode(void) {
#ifdef HAVE_TERMIOS_H
	// restore old termios
	if(tcsetattr(STDIN_FILENO, TCSANOW, &old) < 0){
		perror("tcsetattr");
	}
#endif
}

int term_have_input(void) {
#ifdef HAVE_POLL_H
	struct pollfd fd = {
		.fd = STDIN_FILENO,
		.events = POLLIN,
		.revents = 0,
	};
	if (poll(&fd, 1, 50) <= 0) return 0;
	return (fd.revents & POLLIN) != 0;
#else
	return 0;
#endif
}

static int unget_char = EOF;

static int term_getc(void) {
	if (unget_char != EOF) {
		int c = unget_char;
		unget_char = EOF;
		return c;
	}
	char c = 0;
	ssize_t ret;
	while ((ret = read(STDIN_FILENO, &c, 1)) <= 0) {
		if (tvi.interrupted) return '\0';
		if (ret < 0 && errno != EAGAIN && errno != EINTR) return EOF;
		usleep(5000);
	}
	return (unsigned char)c;
}

static void term_ungetc(int c) {
	if (c == EOF) return;
	unget_char = c;
}

int term_get_key(void) {
	int c = term_getc();
	if (c != '\033') return c;
	if (!term_have_input()) return c;
	int c2 = term_getc();
	if (c2 != '[') {
		term_ungetc(c2);
		return c;
	}
	int c3 = term_getc();
	switch (c3) {
	case 'A':
		return KEY_UP;
	case 'B':
		return KEY_DOWN;
	case 'C':
		return KEY_RIGHT;
	case 'D':
		return KEY_LEFT;
	case 'H':
		return KEY_START;
	case 'F':
		return KEY_END;
	default:
		return '\033';
	}
}

int term_is_delete(int c) {
	return c == '\b' || c == 0x7f || c == 127 || c == 8;
}

int term_enter_fullscreen(void) {
#ifdef HAVE_ISATTY
	if (!isatty(STDOUT_FILENO)) return -1;
#endif
	printf(ESC"[?1049h");
	term_send_code(TERM_CLEAR);
	printf(ESC"[H");
	fflush(stdout);
	return 0;
}

void term_exit_fullscreen(void) {
	term_send_code(TERM_CLEAR);
	printf(ESC"[H");
	printf(ESC"[?1049l");
	fflush(stdout);
}

static int cell_equal(cell_t *a, cell_t *b) {
	return a->c == b->c && a->attr == b->attr;
}

static void print_cells(cell_t *cell, int len) {
	int attr = 0;
	for (int i=0; i<len; i++) {
		if (cell->attr != attr) {
			term_reset_color();
			if (cell->attr & TERM_ATTR_INVERSE) {
				term_send_code(TERM_COLOR_INVERSE);
			}
			if (cell->attr & TERM_ATTR_BOLD) {
				term_send_code(TERM_COLOR_BOLD);
			}
			if (cell->attr & TERM_ATTR_FG) {
				term_send_code(TERM_COLOR_SET_FG, cell->attr & TERM_ATTR_FG_MASK);
			}
			if (cell->attr & TERM_ATTR_BG) {
				term_send_code(TERM_COLOR_SET_BG, (cell->attr & TERM_ATTR_BG_MASK) >> TERM_ATTR_BG_SHIFT);
			}
			attr = cell->attr;
		}
		putchar(cell->c);
		cell++;
	}
	term_reset_color();
}

static void redraw_line(int y) {
	if (y < 0 || y >= term_height) return;
	cell_t *old_line = cell(old_buffer, 0, y);
	cell_t *new_line = cell(new_buffer, 0, y);
	int diff_start = 0;
	while (diff_start < term_width) {
		if (!cell_equal(&old_line[diff_start], &new_line[diff_start])) {
			break;
		}
		diff_start++;
	}

	if (diff_start == term_width) {
		return;
	}

	int diff_end = term_width;
	while (diff_end > diff_start) {
		if (!cell_equal(&old_line[diff_end - 1], &new_line[diff_end - 1])) {
			break;
		}
		diff_end--;
	}

	term_goto(diff_start, y);
	print_cells(&new_line[diff_start], diff_end - diff_start);
	memcpy(old_line, new_line, term_width * sizeof(cell_t));
}

static int line_equal(cell_t *a, cell_t *b) {
	for (int i=0; i<term_width; i++) {
		if (!cell_equal(&a[i], &b[i])) {
			return 0;
		}
	}
	return 1;
}

void term_redraw(void) {
	for (int y=0; y<term_height; y++) {
		if (line_equal(line(old_buffer, y), line(new_buffer, y))) {
			continue;
		}
		redraw_line(y);
	}
	term_goto(cursor_x, cursor_y);
	fflush(stdout);
}

void term_vprint_bound_at(bound_t *bound, int x, int y, int attr, const char *fmt, va_list args) {
	char buf[LINE_MAX];
	int len = vsnprintf(buf, sizeof(buf), fmt, args);

	if (bound) {
		x += bound->x;
		y += bound->y;
	}
	for (int i=0; i<len; i++) {
		switch (buf[i]) {
		case '\t':
			x += 8 - (x % 8);
			continue;
		case '\n':
			x = bound ? bound->x : 0;
			y++;
			continue;
		}
		if (x >= term_width) {
			x = bound ? bound->x : 0;
			y++;
		}
		if (y >= term_height) break;
		if (x < term_width && y < term_height) {
			cell_t *cell = cell(new_buffer, x, y);
			cell->c = buf[i];
			cell->attr = attr;
		}
		x++;
	}
}

void term_vprint_at(int x, int y, int attr, const char *fmt, va_list args) {
	term_vprint_bound_at(NULL, x, y, attr, fmt, args);
}

void term_print_at(int x, int y, int attr, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	term_vprint_at(x, y, attr, fmt, args);
	va_end(args);
}

void term_set_cursor(int x, int y) {
	cursor_x = x;
	cursor_y = y;
}

void term_clear_line(int y) {
	cell_t *cell = cell(new_buffer, 0, y);
	for (int i=0; i<term_width; i++) {
		cell->c = ' ';
		cell->attr = 0;
		cell++;
	}
}

void term_goto(int x, int y) {
	term_send_code(TERM_GOTO, y+1, x+1);
}

void term_bell(void) {
	// No-op to avoid printing unhandled ASCII 7 to TTY
}

void term_reset_color(void) {
	term_send_code(TERM_COLOR_RESET);
}

void term_error_color(void) {
	printf(ESC"[0;41;37m");
}
