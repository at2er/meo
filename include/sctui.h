/** Simple C terminal user interface
 *
 * Put 'SCTUI_IMPL' to one source file to compile it and use it.
 *
 * Some function will immediately write the control sequence to stdout.
 *
 * Usage: See function declarations.
 *
 * Version:
 *     0.1.1: fix(sctui.h): backspace code.
 *     0.1.0
 *
 * MIT License
 *
 * Copyright (c) 2026 at2er <xb0515@outlook.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef SCTUI_H
#define SCTUI_H
#include <stddef.h>
#include <stdint.h>
#include <termios.h>

#ifndef UTILSH_DARR_H
#define UTILSH_DARR_H
#include <stddef.h>
#include <string.h>

#ifndef UTILSH_DARR_REALLOC
#define UTILSH_DARR_REALLOC realloc
#include <stdlib.h>
#endif

#define darr(TYPE) \
	struct { \
		TYPE *e; \
		int n; \
	}

#define darr_append(DARR, ELEM) \
	do { \
		darr_expand(DARR); \
		darr_last(DARR) = (ELEM); \
	} while (0)

#define darr_expand(DARR) \
	darr_resize((DARR), (DARR)->n + 1);

#define darr_init(DARR) \
	do { \
		(DARR)->n = 0; \
		(DARR)->e = NULL; \
	} while (0)

#define darr_last(DARR) ((DARR)->e[(DARR)->n - 1])

#define darr_reduce(DARR) \
	do { \
		if ((DARR)->n <= 0) \
			break; \
		darr_resize((DARR), (DARR)->n - 1); \
	} while (0)

#define darr_remove(DARR, POS) \
	do { \
		if ((DARR)->n <= 0) \
			break; \
		memmove((DARR)->e + (POS), \
				(DARR)->e + (POS) + 1, \
				(DARR)->n - (POS)); \
		darr_resize((DARR), (DARR)->n - 1); \
	} while (0)

#define darr_resize(DARR, N) \
	do { \
		if ((DARR)->n == (N)) \
			break; \
		(DARR)->n = (N); \
		(DARR)->e = UTILSH_DARR_REALLOC((DARR)->e, \
				(DARR)->n * sizeof(*(DARR)->e)); \
	} while (0)

#endif

#define TK_CTRL(K) ((K) & 0x1f)
#define SCTUI_OUT_BUF_SIZ 8192

/* see 'man 4 console_codes'
 *
 *               22  4  4
 * |b b b b b b|0..|bx|bx|
 *  | | | | | |     |  |
 *  | | | | | |     |  FG (0..7) (highest bit is the toggle)
 *  | | | | | |     BG (0..7)    (highest bit is the toggle)
 *  | | | | | reverse
 *  | | | | blink
 *  | | | underscore
 *  | | italic
 *  | half-bright
 *  bold */
#define SCTUI_FG(ATTR) ((ATTR) & 0x00000008)
#define SCTUI_BG(ATTR) ((ATTR) & 0x00000080)
#define SCTUI_SET_FG(FG) (0x00000008 | (FG))
#define SCTUI_SET_BG(BG) (0x00000080 | ((BG) << 4))

#define SCTUI_REVERSE     0x04000000
#define SCTUI_BLINK       0x08000000
#define SCTUI_UNDERSCORE  0x10000000
#define SCTUI_ITALIC      0x20000000
#define SCTUI_HALF_BRIGHT 0x40000000
#define SCTUI_BOLD        0x80000000

enum {
	SCTUI_BLACK,
	SCTUI_RED,
	SCTUI_GREEN,
	SCTUI_BROWN,
	SCTUI_BLUE,
	SCTUI_MAGENTA,
	SCTUI_CYAN,
	SCTUI_WHITE
};

typedef darr(struct sctui_win *) sctui_wins_arr;
struct sctui {
	unsigned int init:1, in_alt_scr:1;

	struct termios cur, orig;
	int cx, cy, w, h;

	char buf[SCTUI_OUT_BUF_SIZ];
	int bufp;

	char cbuf[128];

	sctui_wins_arr wins;
};

struct sctui_win {
	int x, y, w, h;

	/* [height][width (default, you can realloc() it to a bigger memory)],
	 * it white space will be filled */
	char **lines;

	int **attrs;
};

/* It use the 'global_sctui.cbuf', so it unsupports recursive use */
extern const char *sctui_attr_off(void);

/* It use the 'global_sctui.cbuf', so it unsupports recursive use */
extern const char *sctui_attr_on(int attr);

extern void sctui_commit(void);
extern void sctui_commit_win(struct sctui_win *win);
extern void sctui_commit_wins(void);
extern void sctui_fill_space(char *str, int len, int w);
extern void sctui_fini(void);
extern int  sctui_get_y_in(struct sctui_win *win, int y);
extern int  sctui_grab_key(void);
extern void sctui_init(void);
extern void sctui_move(struct sctui_win *win, int x, int y);
extern void sctui_move_win(struct sctui_win *win, int x, int y);
extern void sctui_out(const char *str, int len);
extern void sctui_outc(char c);
extern void sctui_remove_win(struct sctui_win *win);
extern void sctui_resize_win(struct sctui_win *win, int w, int h);
extern void sctui_set_attr(struct sctui_win *win,
		int x, int y, int len, int attr);
extern void sctui_text(struct sctui_win *win, int x, int y,
		const char *str, int len);
extern void sctui_update(void);
extern void sctui_win(struct sctui_win *win);

extern void sctui_close_alt_screen(void);
extern void sctui_open_alt_screen(void);

extern struct sctui global_sctui;

#endif

#ifdef SCTUI_IMPL
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef sctui_calloc
#define sctui_calloc calloc
#endif

#ifndef sctui_free
#define sctui_free free
#endif

#ifndef sctui_realloc
#define sctui_realloc realloc
#endif

#define ESC_CLEAR_SCREEN "\x1b[2J"
#define ESC_SGR_RESET "0"
#define ESC_SGR_BEG   "\x1b["
#define ESC_SGR_END   "m"
#define ESC_MOVE(R,L) "\x1b[" #R ";" #L "f"
#define ESC_MOVEF     "\x1b[%d;%df"

#define ESC_CLOSE_ALT_SCREEN "\x1b[?1049l"
#define ESC_OPEN_ALT_SCREEN  "\x1b[?1049h"

#define ESC_HIDE_CURSOR      "\x1b[?25l"
#define ESC_SHOW_CURSOR      "\x1b[?25h"

#define SCTUI_MIN(A, B) ((A) < (B) ? (A) : (B))

struct sctui global_sctui;

static void _sctui_die(const char *msg, ...);

void
_sctui_die(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);

	if (global_sctui.init)
		sctui_fini();
	exit(1);
}

const char *
sctui_attr_off(void)
{
	strcpy(global_sctui.cbuf, ESC_SGR_BEG
			ESC_SGR_RESET
			ESC_SGR_END);
	return global_sctui.cbuf;
}

const char *
sctui_attr_on(int attr)
{
	char *s = global_sctui.cbuf;

	if (!attr)
		return NULL;

	s += sprintf(s, ESC_SGR_BEG);

	if (SCTUI_BOLD & attr)
		s += sprintf(s, ";1");
	if (SCTUI_HALF_BRIGHT & attr)
		s += sprintf(s, ";2");
	if (SCTUI_ITALIC & attr)
		s += sprintf(s, ";3");
	if (SCTUI_UNDERSCORE & attr)
		s += sprintf(s, ";4");
	if (SCTUI_BLINK & attr)
		s += sprintf(s, ";5");
	if (SCTUI_REVERSE & attr)
		s += sprintf(s, ";7");

	if (SCTUI_BG(attr))
		s += sprintf(s, ";%d", 40 + ((attr & 0x70) >> 4));
	if (SCTUI_FG(attr))
		s += sprintf(s, ";%d", 30 + (attr & 0x07));
	s += sprintf(s, ESC_SGR_END);
	return global_sctui.cbuf;
}

void
sctui_commit(void)
{
	write(STDOUT_FILENO, global_sctui.buf, global_sctui.bufp);
	global_sctui.bufp = 0;
}

void
sctui_commit_win(struct sctui_win *win)
{
	int ocx = global_sctui.cx, ocy = global_sctui.cy,
	    y = win->y;
	int old_attr = 0, attr;

	for (int i = 0; i < win->h; i++, y++) {
		sctui_move(NULL, win->x, y);
		for (int x = 0; x < win->w; x++) {
			attr = win->attrs[i][x];
			if (attr != old_attr) {
				sctui_out(sctui_attr_off(), 0);
				sctui_out(sctui_attr_on(attr), 0);
			}
			sctui_outc(win->lines[i][x]);
			old_attr = win->attrs[i][x];
		}
		if (attr)
			sctui_out(sctui_attr_off(), 0);
	}
	sctui_move(NULL, ocx, ocy);
}

void
sctui_commit_wins(void)
{
	for (int i = 0; i < global_sctui.wins.n; i++)
		sctui_commit_win(global_sctui.wins.e[i]);
}

void
sctui_fill_space(char *str, int len, int w)
{
	for (int i = len; i < w; i++)
		str[i] = ' ';
}

void
sctui_fini(void)
{
	assert(global_sctui.init);
	if (global_sctui.in_alt_scr) {
		sctui_close_alt_screen();
		sctui_commit();
	}
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_sctui.orig);
}

int
sctui_get_y_in(struct sctui_win *win, int y)
{
	if (y > win->h)
		y = win->h - 1;
	return y;
}

int
sctui_grab_key(void)
{
	char buf[1];
	read(STDIN_FILENO, buf, 1);
	return *buf;
}

void
sctui_init(void)
{
	if (global_sctui.init)
		_sctui_die("[global_sctui]: initialized\n");

	tcgetattr(STDIN_FILENO, &global_sctui.orig);
	global_sctui.cur = global_sctui.orig;
	global_sctui.cur.c_cflag |= CS8;
	global_sctui.cur.c_iflag &= ~(IXON | ICRNL);
	global_sctui.cur.c_lflag &= ~(ECHO | ICANON | ISIG);
	global_sctui.cur.c_oflag &= ~OPOST;
	global_sctui.cur.c_cc[VMIN] = 0;
	global_sctui.cur.c_cc[VTIME] = 1;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_sctui.cur);
	sctui_update();
	global_sctui.cx = global_sctui.cy = 0;
	global_sctui.bufp = 0;
	global_sctui.init = 1;
}

void
sctui_move(struct sctui_win *win, int x, int y)
{
	char b[32];

	if (win) {
		x += win->x;
		y += win->y;
	}

	sctui_out(b, sprintf(b, ESC_MOVEF, y + 1, x + 1));
	global_sctui.cx = x;
	global_sctui.cy = y;
}

void
sctui_move_win(struct sctui_win *win, int x, int y)
{
	win->x = x;
	win->y = y;
}

void
sctui_out(const char *str, int len)
{
	if (!str)
		return;
	if (len == 0)
		len = strlen(str);
	if (len > (int)sizeof(global_sctui.buf) - global_sctui.bufp)
		sctui_commit();
	memcpy(global_sctui.buf + global_sctui.bufp, str, len);
	global_sctui.bufp += len;
}

void
sctui_outc(char c)
{
	if ((int)sizeof(global_sctui.buf) <= global_sctui.bufp)
		sctui_commit();
	global_sctui.buf[global_sctui.bufp] = c;
	global_sctui.bufp++;
}

void
sctui_remove_win(struct sctui_win *win)
{
	for (int i = 0; i < global_sctui.wins.n; i++) {
		if (global_sctui.wins.e[i] == win)
			darr_remove(&global_sctui.wins, i);
	}
}

void
sctui_resize_win(struct sctui_win *win, int w, int h)
{
	win->w = w;
	win->h = h;

	win->lines = sctui_realloc(win->lines,
			win->h * sizeof(*win->lines));
	win->attrs = sctui_realloc(win->attrs,
			win->h * sizeof(*win->attrs));

	memset(win->lines, 0, win->h * sizeof(*win->lines));
	memset(win->attrs, 0, win->h * sizeof(*win->lines));

	for (int i = 0; i < win->h; i++) {
		win->lines[i] = sctui_realloc(
				win->lines[i],
				(win->w + 1) * sizeof(**win->lines));
		win->attrs[i] = sctui_realloc(
				win->attrs[i],
				(win->w + 1) * sizeof(**win->attrs));
		if (!win->lines[i] || !win->attrs[i])
			goto err_free_lines;
		memset(win->lines[i], ' ', win->w);
		memset(win->attrs[i], 0,   win->w * sizeof(**win->attrs));
		win->lines[i][win->w] = '\0';
	}

	return;

err_free_lines:
	for (int i = 0; i < win->h; i++) {
		if (win->lines[i])
			sctui_free(win->lines[i]);
		if (win->attrs[i])
			sctui_free(win->attrs[i]);
	}
	sctui_free(win->lines);
	sctui_free(win->attrs);
}

void
sctui_set_attr(struct sctui_win *win, int x, int y, int len, int attr)
{
	if (len == 0 || x >= win->w)
		return;
	if (x + len > win->w)
		len = win->w - x;
	y = sctui_get_y_in(win, y);
	for (int end = x + len; x < end; x++)
		win->attrs[y][x] = attr;
}

void
sctui_text(struct sctui_win *win, int x, int y, const char *str, int len)
{
	int ocx = global_sctui.cx, ocy = global_sctui.cy;

	if (len == 0)
		len = strlen(str);

	if (!win) {
		sctui_move(NULL, x, y);
		sctui_out(str, len);
		sctui_move(NULL, ocx, ocy);
		return;
	}

	if (x >= win->w)
		return;
	if (x + len > win->w)
		len = win->w - x;
	y = sctui_get_y_in(win, y);
	memcpy(win->lines[y] + x, str, len);
	if (x + len < win->w)
		memset(win->lines[y] + x + len, ' ',
				win->w - x - len);
}

void
sctui_update(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		sctui_fini();
		_sctui_die("[global_sctui]: failed to get winsize\n");
	}
	global_sctui.w = ws.ws_col ? ws.ws_col : 80;
	global_sctui.h = ws.ws_row ? ws.ws_row : 24;
}

void
sctui_win(struct sctui_win *win)
{
	win->attrs = NULL;
	win->lines = NULL;
	darr_append(&global_sctui.wins, win);
}

void
sctui_close_alt_screen(void)
{
	sctui_commit();
	sctui_out(ESC_CLOSE_ALT_SCREEN, 0);
	global_sctui.in_alt_scr = 0;
}

void
sctui_open_alt_screen(void)
{
	sctui_out(ESC_OPEN_ALT_SCREEN
			ESC_CLEAR_SCREEN
			ESC_MOVE(1,1),
			0);
	global_sctui.in_alt_scr = 1;
}

#endif /* SCTUI_IMPL */
