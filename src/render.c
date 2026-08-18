#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <tvi.h>	

static size_t render_len(const char *str) {
	if (!str) return 0;
	size_t len = 0;
	while (*str) {
		if (*str == '\t') {
			len += 8 - (len % 8);
		} else {
			len++;
		}
		str++;
	}
	return len;
}

static size_t get_line_height(win_t *win, const char *line) {
	if (!win || win->width <= 0) return 1;
	size_t line_len = render_len(line);
	size_t lines_count = (line_len + win->width - 1) / win->width;
	if (lines_count == 0) lines_count = 1;
	return lines_count;
}

static int get_line_y(win_t *win, int index) {
	int y = 0;
	for (int i=win->scroll; i<index ;i++) {
		if (i >= win->lines_count) {
			y++;
			continue;
		}
		char *line = win->text[i];
		y += get_line_height(win, line);
	}
	return y;
}

void win_print_at(win_t *win, int x, int y, int attr, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	bound_t bound = {
		.x = win->x,
		.y = win->y,
		.width  = win->width,
		.height = win->height,
	};
	term_vprint_bound_at(&bound, x, y, attr, fmt, args);
	va_end(args);
}

int render_line(tvi_t *tvi, win_t *win, size_t index) {
	(void)tvi;
	if (index < (size_t)win->scroll) return -1;
	int y = get_line_y(win, index);
	if (y >= win->height - 1) {
		return -1;
	}
	int line_x = win->x;
	int line_y = win->y + y;
	if (index >= (size_t)win->lines_count) {
		term_clear_line(line_y);
		term_print_at(line_x, line_y, TERM_ATTR_FG_BLUE, "~");
		return 0;
	}
	char *line = win->text[index];
	size_t line_height = get_line_height(win, line);
	if (y + (int)line_height > win->height - 1) {
		term_clear_line(line_y);
		term_print_at(line_x, line_y, TERM_ATTR_FG_BLUE, "@@@");
	} else {
		for (size_t i=0; i<line_height; i++) {
			term_clear_line(line_y+i);
		}
		if (win->syntax) {
			syntax_print_line(win, y, win->syntax, line);
		} else {
			win_print_at(win, 0, y, 0, "%s", line);
		}
	}
	return 0;
}

void render_text(tvi_t *tvi, win_t *win) {
	(void)tvi;
	int cur_y = 0;
	size_t idx = win->scroll;
	while (cur_y < win->height - 1) {
		if (idx < (size_t)win->lines_count) {
			char *line = win->text[idx];
			size_t line_height = get_line_height(win, line);
			if (cur_y + (int)line_height > win->height - 1) {
				term_clear_line(win->y + cur_y);
				term_print_at(win->x, win->y + cur_y, TERM_ATTR_FG_BLUE, "@@@");
				cur_y++;
				break;
			}
			for (size_t i = 0; i < line_height; i++) {
				term_clear_line(win->y + cur_y + i);
			}
			if (win->syntax) {
				syntax_print_line(win, cur_y, win->syntax, line);
			} else {
				win_print_at(win, 0, cur_y, 0, "%s", line);
			}
			cur_y += (int)line_height;
			idx++;
		} else {
			term_clear_line(win->y + cur_y);
			term_print_at(win->x, win->y + cur_y, TERM_ATTR_FG_BLUE, "~");
			cur_y++;
		}
	}
}

void render_status(tvi_t *tvi, win_t *win) {
	(void)tvi;
	int status_x = win->x;
	int status_y = win->y + win->height - 1;
	int y = win->cursor_y;
	if (y < 0) y = 0;
	if (y >= win->lines_count) y = win->lines_count - 1;
	int x = win->cursor_x;
	size_t line_len = win->text[y] ? strlen(win->text[y]) : 0;
	if ((size_t)x > line_len) x = (int)line_len;

	const char *file = (win->files && win->file_index < win->files_count) ? win->files[win->file_index] : NULL;
	if (!file) file = "[NO NAME]";

	size_t max_len = (win->width > 12) ? win->width - 12 : 1;
	char buf[1024];
	if (strlen(file) > max_len) {
		file += strlen(file) - max_len;
		snprintf(buf, sizeof(buf), "<%s %d,%d", file, y+1, x+1);
	} else {
		snprintf(buf, sizeof(buf), "%s %d,%d", file, y+1, x+1);
	}

	term_print_at(status_x, status_y, TERM_ATTR_INVERSE, "% -*s tvi", (term_width > 4) ? term_width - 4 : 1, buf);
}

void render_window(tvi_t *tvi, win_t *win) {
	if (tvi->mode != MODE_VISUAL) return;
	render_text(tvi, win);
	render_status(tvi, win);
}

void render_all_windows(tvi_t *tvi) {
	if (tvi->mode != MODE_VISUAL) return;
	for (win_t *win=tvi->first_window; win; win=win->next) {
		render_window(tvi, win);
	}
}

void render_cursor(tvi_t *tvi) {
	if (tvi->flags & FLAG_PROMPT) {
		term_set_cursor(tvi->prompt_cursor, term_height-1);
		return;
	}
	win_t *win = tvi->focus_window;
	int x = win->cursor_x;
	int y = win->cursor_y;
	if (y < 0) y = 0;
	if (y >= win->lines_count) y = win->lines_count - 1;
	const char *line = win->text[y] ? win->text[y] : "";
	size_t line_len = strlen(line);
	if ((size_t)x > line_len) x = (int)line_len;
	int screen_x = 0;
	for (int i=0; i<x; i++) {
		if (line[i] == '\t') {
			screen_x += 8 - (screen_x % 8);
		} else {
			screen_x++;
		}
	}
	int screen_y = get_line_y(win, y);
	term_set_cursor(win->x + screen_x, win->y + screen_y);
}

void render_prompt(tvi_t *tvi) {
	if (tvi->flags & FLAG_PROMPT) {
		term_print_at(0, term_height-1, 0, "% -*.*s", term_width, (int)tvi->prompt_len, tvi->prompt);
	} else {
		term_print_at(0, term_height-1, 0, "% *s", term_width,  "");
	}
}

void render_flush(tvi_t *tvi) {
	if (tvi->mode != MODE_VISUAL) return;
	render_cursor(tvi);
	term_redraw();
}

void error(tvi_t *tvi, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	if (tvi->mode == MODE_VISUAL) term_goto(0, term_height-1);
	term_reset_color();
	term_error_color();
	vprintf(fmt, args);
	va_end(args);
	term_reset_color();
	if (tvi->mode == MODE_EX) putchar('\n');
}

void print(tvi_t *tvi, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	if (tvi->mode == MODE_VISUAL) {
		term_clear_line(term_height-1);
		term_vprint_at(0, term_height-1, 0, fmt, args);
	} else {
		vprintf(fmt, args);
		if (fmt[strlen(fmt)-1] != '\n') putchar('\n');
	}
	va_end(args);
}
