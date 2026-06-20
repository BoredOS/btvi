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
#include <stdio.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
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

static const char *codes[TERM_CODES_COUNT] = {
	[TERM_GOTO]           = ESC"[%d;%df",
	[TERM_CLEAR_END_LINE] = ESC"[K",
	[TERM_INSERT]         = ESC"[%d@",
	[TERM_DELETE]         = ESC"[%dP",
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
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);
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
	};
	if (poll(&fd, 1, 0) < 0) return 0;
	return fd.revents & POLLIN;
#else
	return 0;
#endif
}

int term_get_key(void) {
	int c = getchar();
	if (c != '\033') return c;
	if (!term_have_input()) return c;
	int c2 = getchar();
	if (c2 != '[') {
		ungetc(c2, stdin);
		return c;
	}
	int c3 = getchar();
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
#ifdef HAVE_TERMIOS_H
	return c == old.c_cc[VERASE];
#else
	return c == '\b' || c == 0x7f;
#endif
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
				term_inverse_color();
			}
			attr = cell->attr;
		}
		putchar(cell->c);
		cell++;
	}
	term_reset_color();
}

static void redraw_line(int y) {
	cell_t *old_line = &old_buffer[cell(0, y)];
	cell_t *new_line = &new_buffer[cell(0, y)];
	int diff_start = 0;
	while (diff_start < term_width) {
		if (!cell_equal(&old_line[diff_start], &new_line[diff_start])) {
			break;
		}
		diff_start++;
	}

	if (diff_start == term_width) {
		// no diff nothing to redraw
		return;
	}

	int new_len = term_width;
	while (new_len > diff_start+1) {
		if (new_line[new_len-1].c != ' ') {
			break;
		}
		new_len--;
	}

	int old_len = term_width;
	while (old_len > diff_start+1) {
		if (old_line[old_len-1].c != ' ') {
			break;
		}
		old_len--;
	}

	int diff_end = new_len;
	for (int old_diff_end=old_len; old_diff_end > diff_start; old_diff_end--) {
		if (diff_end <= diff_start + 1) {
			break;
		}
		if (!cell_equal(&old_line[old_diff_end-1], &new_line[diff_end-1])) {
			break;
		}
		diff_end--;
	}

	int max_len = new_len > old_len ? new_len : old_len;

	int cost_overwrite = max_len - diff_start;
	int cost_clear     = INT_MAX;
	if (term_get_code(TERM_CLEAR_END_LINE) && new_len < old_len) {
		cost_clear = term_get_code_len(TERM_CLEAR_END_LINE) + new_len - diff_start;
	}

	// check insert/deletion
	int cost_insert = INT_MAX;
	int cost_delete = INT_MAX;
	if (term_get_code(TERM_INSERT) && old_len < new_len && diff_end != new_len) {
		// maybee insertion
		cost_insert = term_get_code_len(TERM_INSERT) + diff_end - diff_start;
	}
	if (term_get_code(TERM_DELETE) && old_len > new_len && diff_end != new_len) {
		// maybee deletion
		cost_delete = term_get_code_len(TERM_DELETE) + diff_end - diff_start;
	}

	int best_cost = cost_overwrite;
	if (cost_clear < best_cost) best_cost = cost_clear;
	if (cost_insert < best_cost) best_cost = cost_insert;
	if (cost_delete < best_cost) best_cost = cost_delete;

	term_goto(diff_start, y);
	if (best_cost == cost_overwrite) {
		print_cells(&new_line[diff_start], max_len - diff_start);
	} else if (best_cost == cost_clear) {
		term_send_code(TERM_CLEAR_END_LINE);
		print_cells(&new_line[diff_start], new_len - diff_start);
	} else if (best_cost == cost_insert) {
		term_send_code(TERM_INSERT, new_len - old_len);
		print_cells(&new_line[diff_start], diff_end - diff_start);
	} else if (best_cost == cost_delete) {
		term_send_code(TERM_DELETE, old_len - new_len);
		print_cells(&new_line[diff_start], diff_end - diff_start);
	}
}

void term_redraw(void) {
	// TODO : see if we can scroll/insert/delete lines
	for (int y=0; y<term_height; y++) {
		redraw_line(y);
	}
	memcpy(old_buffer, new_buffer, term_width * term_height * sizeof(cell_t));
	term_goto(cursor_x, cursor_y);
	fflush(stdout);
}

void term_vprint_at(int x, int y, int attr, const char *fmt, va_list args) {
	char buf[LINE_MAX];
	int len = vsnprintf(buf, sizeof(buf), fmt, args);

	for (int i=0; i<len; i++) {
		switch (buf[i]) {
		case '\t':
			x += 8 - (x % 8);
			continue;
		case '\n':
			x = 0;
			y++;
			continue;
		}
		if (x >= term_width) {
			x = 0;
			y++;
		}
		cell_t *cell = &new_buffer[cell(x, y)];
		cell->c = buf[i];
		cell->attr = attr;
		x++;
	}
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
	cell_t *cell = &new_buffer[cell(0, y)];
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
	putchar('\a');
	fflush(stdout);
}

void term_reset_color(void) {
	printf(ESC"[0m");
}

void term_inverse_color(void) {
	printf(ESC"[0;7m");
}

void term_error_color(void) {
	printf(ESC"[0;41;37m");
}
