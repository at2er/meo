#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define UTILSH_LIST_STRIP
#include "darr.h"
#include "getarg.h"
#include "list.h"
#include "sctui.h"

#include "meo.h"
#include "utils.h"

#define ARG(...) (union arg){__VA_ARGS__}
#define refreshl(LREF) (*(LREF)->r = 0)
#define refreshw(WREF) ((WREF)->refresh = 1)

static void draw(void);
static void draw_win(struct win *w);
static void fini(void);
static const struct key *get_keys_table(void);
static void init(void);
static void keypress(void);
static void render_line(struct line *l);
static void scroll(struct win *w);

static const char *usages[] = {
"Usage: meo [OPTIONS] [FILE]",
"",
"Options:",
"  -h, --help: show usages",
NULL
};
static struct option opts[] = {
	OPT_HELP("help", 'h', usages),
	OPT_END
};

static struct fbuf *fbs;
static struct tab *tabs;
static struct win *wins;
static int         nfb;
static int         ntab;
static int         nwin;

static struct win *bar;
static struct win *cmdback;
static struct fbuf cmdbuf;

/* state */
static int         cmode = 'n';
static struct tab *ctab;

static struct pollfd fds[1];
static char sbuf[BUFSIZ];
static int running = 1;

static const char *entry;

#include "config.h"

void
draw(void)
{
	int col = 0;
	for (int i = 0; i < nwin; i++)
		draw_win(&wins[i]);
	for (int i = 0; i < ctab->w->col; i++) {
		switch (ctab->w->l->s.s[i]) {
		case '\t':
			col += strlen(tab_render);
			break;
		default:
			col++;
			break;
		}
	}
	sctui_move(ctab->w->x + col,
			ctab->w->y + ctab->w->row - ctab->w->rowoff);
	sctui_commit();
}

void
draw_win(struct win *w)
{
	int i = 0;

	if (!w->refresh)
		return;

	list_for_each(struct line, l, &w->draw->link, tmp, link) {
		if (i >= w->h)
			break;
		render_line(l);
		sctui_text(w->x, w->y + i, l->r, w->w);
		i++;
	}

	for (; i < w->h; i++) {
		sctui_fill_space(sbuf, 0, w->w);
		sctui_text(w->x, w->y + i, sbuf, w->w);
	}

	w->refresh = 0;
}

void
fini(void)
{
	sctui_fini();
}

const struct key *
get_keys_table(void)
{
	switch (cmode) {
	case 'n':
		return normal_keys;
	case 'c':
		return cmd_keys;
	case 'i':
		return insert_keys;
	}

	die("get_keys_table()");

	return NULL;
}

void
init(void)
{
	struct line *l;
	sctui_init();
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

	nwin = 2;
	wins = erealloc(wins, sizeof(*wins) * nwin);

	ntab = 1;
	tabs = erealloc(tabs, sizeof(*tabs) * ntab);
	ctab = tabs;
	ctab->w = wins + 1;
	ctab->w->w = global_sctui.w;
	ctab->w->h = global_sctui.h - 1;

	l = ecalloc(1, sizeof(*l));
	estr_from_cstr(&l->s, "\n");
	refreshl(l);

	bar = wins;
	memset(bar, 0, sizeof(*bar));
	bar->x = 0;
	bar->y = global_sctui.h - 1;
	bar->w = global_sctui.w;
	bar->h = 1;
	bar->fb = &cmdbuf;
	bar->fb->nline = 1;
	bar->l = bar->draw = l;
	list_init(&bar->fb->lines);
	list_insert(&bar->fb->lines, bar->fb->lines.end, &l->link);
	refreshw(bar);

	if (!entry)
		cmd_edit(0, NULL);
	else
		cmd_edit(2, (const char*[]){"e", entry});
}

void
keypress(void)
{
	char *buf;
	int k;

	if (!(fds[0].revents & POLLIN))
		return;

	k = sctui_grab_key();
	if (skb_handle_key(k)) {
		if (cmode == 'i' || cmode == 'c')
			return;
		skb_ncombo = 0;
		return;
	}
	if (!(cmode == 'i' || cmode == 'c')) {
		skb_ncombo = 0;
		return;
	}

	buf = sbuf;
	for (int i = 0; i < skb_ncombo; i++) {
		*buf = skb_combo[i];
		buf++;
	}
	*buf = '\0';
	skb_ncombo = 0;

	insert(&ARG(.s = sbuf));
}

void
new_line(const union arg *arg)
{
	struct line *l, *prv = ctab->w->l;
	struct str s;

	l = ecalloc(1, sizeof(*l));
	if (!(arg->i & 1))
		prv = list_container_of(prv->link.prv, struct line, link);
	list_insert(&ctab->w->fb->lines, &prv->link, &l->link);
	ctab->w->fb->nline++;

	if (arg->i & 1 << 1) {
		s.s = prv->s.s + ctab->w->col;
		s.len = prv->s.len - ctab->w->col;
		s.siz = s.len + 1;
		if (s.len > 0)
			estr_append_str(&l->s, &s);
		estr_remove(&prv->s, ctab->w->col, s.len - 1);
	} else {
		estr_from_cstr(&l->s, "\n");
	}

	move_row(&ARG(.i = (arg->i & 1)));

	refreshl(prv);
	refreshl(l);
	refreshw(ctab->w);
}

void
render_line(struct line *l)
{
	int p = 0, t;

	if (*l->r != 0)
		return;

	for (int i = 0; p < VLINE_RENDER_MAX; i++) {
		if (i >= (int)l->s.len)
			goto space;
		switch (l->s.s[i]) {
		case '\t':
			if ((t = strlen(tab_render)) + p >= VLINE_RENDER_MAX)
				t = VLINE_RENDER_MAX - p;
			memcpy(l->r + p, tab_render, t);
			p += t;
			break;
		case '\n':
		space:
			l->r[p] = ' ';
			p++;
			break;
		default:
			l->r[p] = l->s.s[i];
			p++;
			break;
		}
	}
}

void
scroll(struct win *w)
{
	int i = 0, max;

	w->row = align(w->row, 0, w->fb->nline - 1);
	if (w->row <= w->rowoff) {
		w->rowoff = w->row;
		refreshw(w);
	} else if (w->row > w->rowoff + w->h) {
		w->rowoff = w->row - w->h;
		refreshw(w);
	}
	list_for_each(struct line, l,
			w->fb->lines.beg,
			tmp, link) {
		if (i == w->rowoff)
			w->draw = l;
		if (i >= w->row) {
			w->l = l;
			break;
		}
		i++;
	}

	if (w->l)
		max = w->l->s.len - 1;
	else
		max = 0;
	w->col = align(w->col, 0, max);

	w->fb->row = w->row;
	w->fb->rowoff = w->rowoff;
	w->fb->col = w->col;
}

/* key functions */
void
cmd(const union arg *arg)
{
	int argc = 0;
	char **argv = NULL;
	char *tok, *dup, *saver;

	if (arg->s) {
		dup = strdup(arg->s);
	} else {
		dup = strdup(bar->l->s.s);
		bar->l->s.s[0] = '\n';
		bar->l->s.s[1] = '\0';
		bar->l->s.len = 1;
		scroll(bar);
		refreshl(bar->l);
		refreshw(bar);
	}

	for (tok = dup; ; tok = NULL) {
		if (!(tok = strtok_r(tok, " \t\n", &saver)))
			break;
		darr_append(argv, argc, tok);
	}
	darr_append(argv, argc, NULL);
	argc--;

	cmode = 'n';
	ctab->w = cmdback;

	if (!argc || !argv[0])
		return;

	for (int i = 0; cmds[i].cmd != NULL; i++) {
		if (strcmp(cmds[i].cmd, argv[0]) == 0 ||
		    strcmp(cmds[i].alias, argv[0]) == 0) {
			cmds[i].func(argc, (const char**)argv);
			break;
		}
	}

	free(dup);
}

void
delete(const union arg *arg)
{
	struct line *l = ctab->w->l, *prv;
	int pos;
	if (!l)
		return;
	pos = ctab->w->col - arg->i;
	if (pos < 0) {
		prv = list_container_of(l->link.prv, struct line, link);
		if (!prv)
			return;
		ctab->w->col = prv->s.len;

		list_remove(&ctab->w->fb->lines, &l->link);
		move_row(&ARG(.i = -1));

		str_free(&l->s);
		free(l);

		refreshw(ctab->w);
		return;
	}
	estr_remove(&l->s, pos, arg->i);
	refreshl(l);
	refreshw(ctab->w);
	move_col(&ARG(.i = -arg->i));
}

void
insert(const union arg *arg)
{
	const char *c;
	struct line *l = ctab->w->l;
	struct str s;

	s.s = (char*)arg->s;
	s.len = 0;
	for (c = arg->s; *c; c++) {
		if (*c == '\n') {
			estr_insert_str(&l->s, ctab->w->col - 1, &s);
			s.s = (char*)(c + 1);
			s.siz = s.len = 0;
			new_line(&ARG(.i = 0 | 1 << 1));
			continue;
		}
		s.len++;
		ctab->w->col++;
	}

	refreshl(l);
	refreshw(ctab->w);

	estr_insert_str(&l->s, ctab->w->col - 1, &s);
}

void
mode(const union arg *arg)
{
	int orig = cmode;
	cmode = arg->i;
	switch (cmode) {
	case 'c':
		cmdback = ctab->w;
		ctab->w = bar;
		refreshw(ctab->w);
		break;
	default:
		if (orig == 'c') {
			ctab->w = cmdback;
			refreshw(ctab->w);
		}
		break;
	}
}

void
move_col(const union arg *arg)
{
	ctab->w->col += arg->i;
	scroll(ctab->w);
}

void
move_row(const union arg *arg)
{
	ctab->w->row += arg->i;
	scroll(ctab->w);
}

void
quit(const union arg *arg)
{
	running = 0;
}

/* command functions */
void
cmd_edit(int argc, const char *argv[])
{
	struct fbuf *fb;
	FILE *fp;
	struct line *l;
	int nline;

	if (argc > 1 && argv) {
		for (int i = 0; i < nfb; i++) {
			if (strcmp(fbs[i].path, argv[1]) == 0) {
				fb = &fbs[i];
				goto setwin;
			}
		}
	}

	nfb++;
	fbs = erealloc(fbs, sizeof(*fbs) * nfb);
	fb = &fbs[nfb - 1];
	memset(fb, 0, sizeof(*fb));

	list_init(&fb->lines);

	if (argc <= 1 || !argv) {
		strcpy(fb->path, "<unnamed>");
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->s, "\n");
		refreshl(l);
		list_insert(&fb->lines, fb->lines.end, &l->link);
		fb->nline = 1;
		goto setwin;
	}

	strcpy(fb->path, argv[1]);

	if (!(fp = fopen(fb->path, "r")))
		return;

	for (nline = 0; fgets(sbuf, BUFSIZ, fp); nline++) {
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->s, sbuf);
		refreshl(l);
		list_insert(&fb->lines, fb->lines.end, &l->link);
	}
	fb->nline = nline;

	fclose(fp);
setwin:
	ctab->w->row = fb->row;
	ctab->w->rowoff = fb->rowoff;
	ctab->w->col = fb->col;
	ctab->w->fb = fb;
	ctab->w->l = list_container_of(fb->lines.beg, struct line, link);
	scroll(ctab->w);
	refreshw(ctab->w);
}

void
cmd_write(int argc, const char *argv[])
{
	FILE *fp;

	if (argc <= 1 || !argv[1]) {
		if (!(fp = fopen(ctab->w->fb->path, "w")))
			return;
	} else {
		fp = fopen(argv[1], "w");
	}

	list_for_each(struct line, l, ctab->w->fb->lines.beg, tmp, link)
		fputs(l->s.s, fp);

	fclose(fp);
}

void
cmd_quit(int argc, const char *argv[])
{
	quit(NULL);
}

int
main(int argc, char *argv[])
{
	int r;

	GETARG_BEGIN(r, argc, argv, opts) {
	case GETARG_RESULT_SUCCESSFUL:
		break;
	case GETARG_RESULT_UNKNOWN:
		if (entry)
			die("too many entry files\n");
		entry = *argv;
		GETARG_SHIFT(argc, argv);
		break;
	case GETARG_RESULT_APPLIED_HELP_OPT:
	default:
		return 1;
	} GETARG_END;

	init();
	while (running) {
		draw();
		if (poll(fds, 1, -1) == -1 && errno != EINTR)
			die("poll()");
		keypress();
	}
	fini();

	return 0;
}

#define SKB_IMPL
#include "skb.h"
