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
#ifndef LIBSCTUI_H
#define LIBSCTUI_H
#include <stddef.h>
#include <stdint.h>
#include <termios.h>

#define TK_CTRL(K) ((K) & 0x1f)
#define SCTUI_OUT_BUF_SIZ 8196

struct sctui {
	int init;

	struct termios cur, orig;
	int cx, cy, w, h;

	char buf[SCTUI_OUT_BUF_SIZ];
	int bufp;
};

extern void sctui_commit(void);
extern void sctui_fill_space(char *str, int len, int w);
extern void sctui_fini(void);
extern void sctui_get_win(void);
extern int  sctui_grab_key(void);
extern void sctui_init(void);
extern void sctui_move(int x, int y);
extern void sctui_out(const char *str, int len);
extern void sctui_text(int x, int y, const char *str, int len);

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

#define ESC_CLEAR_SCREEN     "\x1b[2J"
#define ESC_MOVE(R,L) "\x1b[" #R ";" #L "f"
#define ESC_MOVEF     "\x1b[%d;%df"

#define ESC_CLOSE_ALT_SCREEN "\x1b[?1049l"
#define ESC_OPEN_ALT_SCREEN  "\x1b[?1049h"

#define ESC_HIDE_CURSOR      "\x1b[?25l"
#define ESC_SHOW_CURSOR      "\x1b[?25h"

struct sctui global_sctui;

static void
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

void
sctui_commit(void)
{
	write(STDOUT_FILENO, global_sctui.buf, global_sctui.bufp);
	global_sctui.bufp = 0;
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
	write(STDOUT_FILENO, ESC_CLOSE_ALT_SCREEN, 8);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_sctui.orig);
}

void
sctui_get_win(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		sctui_fini();
		_sctui_die("[global_sctui]: failed to get winsize\n");
	}
	global_sctui.w = ws.ws_col ? ws.ws_col : 80;
	global_sctui.h = ws.ws_row ? ws.ws_row : 24;
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
	sctui_get_win();
	global_sctui.cx = global_sctui.cy = 0;
	global_sctui.bufp = 0;
	write(STDOUT_FILENO,
			ESC_OPEN_ALT_SCREEN
			ESC_CLEAR_SCREEN
			ESC_MOVE(1,1),
			strlen(ESC_CLEAR_SCREEN)
			+ 14);
	global_sctui.init = 1;
}

void
sctui_move(int x, int y)
{
	char b[32];
	sctui_out(b, sprintf(b, ESC_MOVEF, y + 1, x + 1));
	global_sctui.cx = x;
	global_sctui.cy = y;
}

void
sctui_out(const char *str, int len)
{
	if (len == 0)
		len = strlen(str);
	if (len > (int)sizeof(global_sctui.buf) - global_sctui.bufp)
		sctui_commit();
	memcpy(global_sctui.buf + global_sctui.bufp, str, len);
	global_sctui.bufp += len;
}

void
sctui_text(int x, int y, const char *str, int len)
{
	int ox = global_sctui.cx, oy = global_sctui.cy;
	sctui_move(x, y);
	sctui_out(str, len);
	sctui_move(ox, oy);
}

#endif /* SCTUI_IMPL */
