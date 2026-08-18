#include <stdlib.h>
#include <string.h>
#include <tvi.h>

// tools to manipulate text
void text_mark_dirty(win_t *win) {
	if (win) win->flags |= FLAG_DIRTY;
}

void text_insert_lines(win_t *win, int addr, char *const*lines, size_t lines_count) {
	if (!win || lines_count == 0) return;
	if (addr < 0) addr = 0;
	if (addr > win->lines_count) addr = win->lines_count;

	text_mark_dirty(win);
	win->text = realloc(win->text, sizeof(char*) * (win->lines_count + lines_count));
	memmove(&win->text[addr+lines_count], &win->text[addr], (win->lines_count-addr)*sizeof(char*));
	win->lines_count += lines_count;
	for (size_t i=0; i<lines_count; i++) {
		win->text[addr+i] = strdup(lines[i] ? lines[i] : "");
	}
}

void text_insert_newline(win_t *win, int x, int y) {
	if (!win || y < 0 || y >= win->lines_count || !win->text[y]) return;
	text_mark_dirty(win);
	char *line = win->text[y];
	size_t len = strlen(line);
	if ((size_t)x > len) x = (int)len;
	char *new_line = &line[x];
	text_insert_lines(win, y+1, &new_line, 1);
	win->text[y][x] = '\0';
}

void text_insert_buf(win_t *win, int x, int y, const char *buf, size_t count) {
	if (!win || y < 0 || y >= win->lines_count || !win->text[y] || count == 0) return;
	text_mark_dirty(win);
	char *line = win->text[y];
	size_t len = strlen(line);
	if ((size_t)x > len) x = (int)len;
	line = realloc(line, len + 1 + count);
	if (!line) return;
	memmove(&line[x+count], &line[x], len - x + 1);
	memcpy(&line[x], buf, count);
	win->text[y] = line;
}

void text_delete_reg(tvi_t *tvi, win_t *win, int x, int y, size_t count, int reg) {
	if (!win || y < 0 || y >= win->lines_count || !win->text[y] || count == 0) return;
	char *line = win->text[y];
	size_t len = strlen(line);
	if ((size_t)x >= len) return;
	if (x + count > len) count = len - x;

	text_mark_dirty(win);
	if (reg) {
		char *buf = &line[x];
		reg_write(tvi, reg, &buf, count, REG_CHAR);
	}
	memmove(&line[x], &line[x+count], len - x - count + 1);
}

void text_delete(win_t *win, int x, int y, size_t count) {
	text_delete_reg(NULL, win, x, y, count, 0);
}

void text_delete_lines_reg(tvi_t *tvi, win_t *win, int addr, size_t count, int reg) {
	if (!win || addr < 0 || count == 0 || addr >= win->lines_count) return;
	if (addr + (int)count > win->lines_count) count = win->lines_count - addr;

	text_mark_dirty(win);
	if (reg) {
		reg_write(tvi, reg, &win->text[addr], count, REG_LINE);
	}
	for (size_t i = 0; i < count; i++) {
		free(win->text[addr + i]);
	}
	memmove(&win->text[addr], &win->text[addr+count], (win->lines_count - addr - count) * sizeof(char*));
	win->lines_count -= count;
	if (win->lines_count == 0) {
		win->text = realloc(win->text, sizeof(char*));
		win->text[0] = strdup("");
		win->lines_count = 1;
	}
}

void text_delete_lines(win_t *win, int addr, size_t count) {
	text_delete_lines_reg(NULL, win, addr, count, 0);
}

void text_join(win_t *win, int first, int last, char sep) {
	if (!win || first < 0 || last >= win->lines_count || first >= last) return;
	size_t new_size = 1;
	if (sep) {
		new_size += (last - first);
	}
	for (int current=first; current<=last; current++) {
		if (win->text[current]) {
			new_size += strlen(win->text[current]);
		}
	}
	char *line = win->text[first];
	line = realloc(line, new_size);
	if (!line) return;
	for (int current=first+1; current<=last; current++) {
		if (sep) {
			char buf[2] = {sep, 0};
			strcat(line, buf);
		}
		if (win->text[current]) {
			strcat(line, win->text[current]);
		}
	}
	win->text[first] = line;
	text_delete_lines(win, first+1, last-first);
}

void text_yank_lines(tvi_t *tvi, win_t *win, int addr, size_t count, int reg) {
	if (!win || addr < 0 || addr >= win->lines_count) return;
	if (addr + (int)count > win->lines_count) count = win->lines_count - addr;
	reg_write(tvi, reg, &win->text[addr], count, REG_LINE);
}
