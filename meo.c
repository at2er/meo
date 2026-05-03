#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define UTILSH_LIST_STRIP
#define UTILSH_DARR_REALLOC erealloc
#include "darr.h"
#include "getarg.h"
#include "list.h"
#include "sctui.h"

#include "meo.h"
#include "utils.h"

#define MAX_MACHES 1

#define ARG(...) (union arg){__VA_ARGS__}
#define lineof(LINK) list_container_of(LINK, struct line, link)
#define undoof(LINK) list_container_of(LINK, struct undo, link)
#define refreshw(WREF) ((WREF)->refresh = 1)

struct selection {
	struct marker *beg, *end;
	int first_len, last_len;
};

static void comp_pattern(const char *p, int len);
static void draw(void);
static void draw_line(struct win *w, struct line *l, int row, int beg, int end);
static void draw_sel(void);
static void draw_win(struct win *w);
static void dup_to_reg(int r, char *s);
static struct undo *edit(struct str *buf, struct edit *e);
static void edit_insert(struct undo *u, struct edit *e);
static void edit_new_line(int updown);
static void empty_fbuf(struct fbuf *fb);
static struct line *empty_line(void);
static void fini(void);
static void get_draw_line(struct win *w);
static const struct key *get_keys_table(void);
static struct line *get_line_nex(struct line *beg, int step);
static struct line *get_line_prv(struct line *beg, int step);
static struct marker *get_marker(int k);
static char get_marker_chr(int idx);
static void get_minmax_marker(struct marker **beg, struct marker **end);
static char **get_reg(int k);
static int get_rx(struct win *w, struct line *l, int col);
static int get_ry(struct win *w, int row);
static void get_sel(struct selection *sel);
static void init(void);
static void jumping(void);
static void keypress(int k);
static int match(const char *str);
static int mode_can_insert(void);
static struct undo *new_undo(struct fbuf *fb);
static void refreshl(struct win *w, struct line *l);
static void remove_fbuf(struct fbuf *fb);
static void remove_line(struct fbuf *fb, struct line *l);
static void render_line(const struct win *w, struct line *l);
static int request_key(void);
static void ruler(void);
static int search_nex(void);
static int search_prv(void);
static void sel_word_nex(const char **beg, const char **end);
static void sel_word_prv(const char **beg, const char **end);
static void set_bar_buf(struct fbuf *fb);
static void set_child(void);
static void set_col(struct win *w, int col);
static void set_row(struct win *w, int row);
static void sys_copy(const char *s);
static char *sys_paste(void);
static struct fbuf *tmp_fbuf(void);
static void xfocus_win(struct win *w);
static void xgoto_mark(struct marker *m);

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

static fb_arr  fbs;
static tab_arr tabs;

static struct win bar;
static struct win *cmdback;
static struct fbuf cmdbuf;
static struct fbuf rulerbuf;

/* '+' */
static char *regs[1];

/* state */
static int         cmode = MODE_NOR;
static struct tab *ctab;
static struct line *has_sel;

/* numbers + lowers + '\'' */
static struct marker markers[10 + 26 + 1];
#define SEL_MARKER markers[10 + 26]

static struct line *matched;
static regmatch_t matches[MAX_MACHES];
static regex_t *pattern;

static struct pollfd fds[1];
static char sbuf[BUFSIZ];
static int running = 1;

static const char *entry;

#include "config.h"

void
comp_pattern(const char *p, int len)
{
	char *dup = (char*)p;
	if (len != 0)
		dup = strndup(p, len);
	if (pattern)
		regfree(pattern);
	if (!pattern)
		pattern = ecalloc(1, sizeof(*pattern));
	if (regcomp(pattern, dup, REG_NEWLINE)) {
		free(pattern);
		pattern = NULL;
	}
}

void
draw(void)
{
	draw_win(&ctab->mw);
	if (ctab->enable_tmpw)
		draw_win(&ctab->tmpw);

	ruler();
	sctui_out(sctui_attr_on(bar_attr), 0);
	draw_win(&bar);
	sctui_out(sctui_attr_off(), 0);

	if (has_sel)
		draw_sel();
	sctui_move(get_rx(ctab->w, ctab->w->p.l, ctab->w->p.col),
			get_ry(ctab->w, ctab->w->p.row));

	sctui_commit();
}

void
draw_line(struct win *w, struct line *l, int row, int beg, int end)
{
	int rx = get_rx(w, l, beg), ry = get_ry(w, row),
	    len = get_rx(w, l, end) - rx;
	sctui_text(rx, ry, l->r + rx, MIN(len, w->w));
}

void
draw_sel(void)
{
	struct line *l;
	struct selection sel;

	sctui_out(sctui_attr_on(sel_attr), 0);

	sel.beg = &SEL_MARKER;
	sel.end = &ctab->w->p;
	get_sel(&sel);
	l = sel.beg->l;
	if (sel.first_len)
		draw_line(ctab->w, l, sel.beg->row, sel.beg->col,
				sel.beg->col + sel.first_len);

	l = lineof(l->link.nex);
	for (int i = sel.beg->row + 1; i < sel.end->row; i++) {
		if (l->s.len <= 0)
			continue;
		draw_line(ctab->w, l, i, 0, l->s.len);
		l = lineof(l->link.nex);
	}

	if (sel.last_len)
		draw_line(ctab->w, l, sel.end->row, 0, sel.last_len);

	sctui_out(sctui_attr_off(), 0);
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
		render_line(w, l);
		draw_line(w, l, w->p.rowoff + i, 0, w->w);
		i++;
	}

	for (; i < w->h; i++) {
		sctui_fill_space(sbuf, 0, w->w);
		sctui_text(w->x, w->y + i, sbuf, w->w);
	}

	w->refresh = 0;
}

void
dup_to_reg(int r, char *s)
{
	char **reg = get_reg(r);
	if (!reg)
		return;
	if (*reg)
		free(*reg);
	*reg = s;

	if (r == '+')
		sys_copy(*reg);
}

struct undo *
edit(struct str *buf, struct edit *e)
{
	struct line *l, *nex;
	struct str _buf;
	struct selection sel;
	struct undo *u;

	u = new_undo(ctab->w->p.fb);
	u->e = *e;

	sel.beg = &e->beg;
	sel.end = &e->end;
	get_sel(&sel);
	u->e.beg = *sel.beg;
	u->e.end = *sel.end;

	if (!buf)
		buf = &_buf;
	str_empty(buf);

	set_row(ctab->w, sel.beg->row);
	l = ctab->w->p.l;

	if (sel.first_len) {
		estr_append_str(buf, &STR(l->s.s + sel.beg->col, sel.first_len));
		estr_remove(&l->s, sel.beg->col, sel.first_len);
		refreshl(ctab->w, l);
	}

	l = lineof(l->link.nex);

	for (int i = sel.beg->row + 1; i < sel.end->row; i++) {
		if (l->s.len <= 0) {
			estr_append_chr(buf, '\n');
			continue;
		}
		estr_append_str(buf, &l->s);
		nex = lineof(l->link.nex);
		remove_line(ctab->w->p.fb, l);
		l = nex;
	}

	if (sel.beg->row != sel.end->row) {
		estr_append_str(buf, &STR(l->s.s, sel.last_len));
		estr_append_cstr(&sel.beg->l->s, l->s.s + sel.last_len);
		remove_line(ctab->w->p.fb, l);
	}

	if (sel.beg->l->s.s[sel.beg->l->s.len - 1] != '\n')
		estr_append_chr(&sel.beg->l->s, '\n');

	if (buf->s)
		estr_from_str(&u->e.replace, buf);

	set_col(ctab->w, sel.beg->col);

	if (!e->replace.s) {
		u->e.end = u->e.beg;
		return u;
	}

	edit_insert(u, e);

	return u;
}

void
edit_insert(struct undo *u, struct edit *e)
{
	char *c;
	struct marker *p = &ctab->w->p;
	struct str s, orig;

	str_empty(&orig);

	s.s = e->replace.s;
	s.len = 0;
	for (c = e->replace.s; *c; c++) {
		if (*c == '\n') {
			if (!orig.s) {
				estr_from_cstr(&orig, p->l->s.s + p->col);
				estr_remove(&p->l->s, p->col, p->l->s.len - p->col);
			}
			estr_append_str(&p->l->s, &s);
			estr_append_chr(&p->l->s, '\n');
			s.s = c + 1;
			s.siz = s.len = 0;
			edit_new_line(1);
			continue;
		}
		s.len++;
		p->col++;
	}
	refreshl(ctab->w, p->l);

	estr_insert_str(&p->l->s, p->col - s.len, &s);
	estr_insert_str(&p->l->s, p->col - s.len + s.len, &orig);
	str_free(&orig);

	set_col(ctab->w, p->col);

	u->e.end = *p;

	/* don't free() it */
	str_empty(&u->e.replace);
}

void
edit_new_line(int updown)
{
	struct marker *p = &ctab->w->p;
	struct line *prv, *l;

	p->fb->ldirty = 1;
	prv = p->l;
	if (updown == 0)
		prv = lineof(prv->link.prv);
	l = ecalloc(1, sizeof(*l));

	list_insert(&p->fb->lines,
			prv ? &prv->link : NULL,
			&l->link);
	p->fb->nline++;

	if (prv)
		refreshl(ctab->w, prv);
	refreshl(ctab->w, l);

	move_row(&ARG(.i = updown));
}

void
empty_fbuf(struct fbuf *fb)
{
	struct line *l = empty_line();
	list_init(&fb->lines);
	list_init(&fb->undo);
	list_insert(&fb->lines, fb->lines.end, &l->link);
	fb->nline = 1;
}

struct line *
empty_line(void)
{
	struct line *l = ecalloc(1, sizeof(*l));
	estr_from_cstr(&l->s, "\n");
	return l;
}

void
fini(void)
{
	sctui_close_alt_screen();
	sctui_commit();
	sctui_fini();
}

void
get_draw_line(struct win *w)
{
	int i = 0, c = w->p.row - w->p.rowoff;
	if (w->p.row == w->p.rowoff) {
		w->draw = w->p.l;
		return;
	}

	list_for_each_prv(struct line, l, &w->p.l->link, tmp, link) {
		if (i >= c) {
			w->draw = l;
			break;
		}
		i++;
	}
}

const struct key *
get_keys_table(void)
{
	switch (cmode) {
	case MODE_NOR:
		return normal_keys;
	case MODE_INS:
		return insert_keys;
	case MODE_CMD:
	case MODE_SEARCH:
		return cmd_keys;
	}

	die("get_keys_table()");

	return NULL;
}

struct line *
get_line_nex(struct line *beg, int step)
{
	list_for_each(struct line, l, &beg->link, tmp, link) {
		if (step <= 0)
			return l;
		step--;
	}
	return NULL;
}

struct line *
get_line_prv(struct line *beg, int step)
{
	list_for_each_prv(struct line, l, &beg->link, tmp, link) {
		if (step <= 0)
			return l;
		step--;
	}
	return NULL;
}

struct marker *
get_marker(int k)
{
	if (k >= '0' && k <= '9') {
		k -= '0';
	} else if (k >= 'a' && k <= 'z') {
		k = k - 'a' + 10;
	} else if (k == '\'') {
		k = &SEL_MARKER - markers;
	} else {
		return NULL;
	}
	return &markers[k];
}

char
get_marker_chr(int idx)
{
	if (idx >= 0 && idx <= 9) {
		idx += '0';
	} else if (idx >= 10 && idx <= 26) {
		idx = idx - 10 + 'a';
	} else if (idx == &SEL_MARKER - markers) {
		idx = '\'';
	}
	return idx;
}

void
get_minmax_marker(struct marker **beg, struct marker **end)
{
	struct marker *b = *beg, *e = *end;
	if (b->row > e->row
	|| (b->row == e->row && b->col > e->col)) {
		*end = b;
		*beg = e;
	}
}

char **
get_reg(int k)
{
	if (k == '+') {
		k = 0;
	} else {
		return NULL;
	}
	return &regs[k];
}

int
get_rx(struct win *w, struct line *l, int col)
{
	int rx = 0;
	for (int i = 0; i < col; i++) {
		switch (l->s.s[i]) {
		case '\t':
			rx += strlen(tab_render);
			break;
		default:
			rx++;
			break;
		}
	}
	return w->x + rx;
}

int
get_ry(struct win *w, int row)
{
	return w->y + row - w->p.rowoff;
}

void
get_sel(struct selection *sel)
{
	get_minmax_marker(&sel->beg, &sel->end);
	if (sel->beg->row != sel->end->row) {
		sel->first_len = sel->beg->l->s.len - sel->beg->col;
		sel->last_len = sel->end->col;
	} else {
		sel->first_len = sel->end->col - sel->beg->col;
		sel->last_len = 0;
	}
}

void
init(void)
{
	struct sigaction sa;

	sctui_init();
	sctui_open_alt_screen();

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	while (waitpid(-1, NULL, WNOHANG) > 0);

	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

	ctab = ecalloc(1, sizeof(*ctab));
	ctab->w = &ctab->mw;
	memset(&ctab->mw, 0, sizeof(ctab->mw));
	memset(&ctab->tmpw, 0, sizeof(ctab->tmpw));

	darr_init(&tabs);
	darr_append(&tabs, ctab);

	ctab->w->w = global_sctui.w;
	ctab->w->h = global_sctui.h - 1;

	empty_fbuf(&rulerbuf);
	empty_fbuf(&cmdbuf);

	memset(&bar, 0, sizeof(bar));
	bar.x = 0;
	bar.y = global_sctui.h - 1;
	bar.w = global_sctui.w;
	bar.h = 1;
	bar.p.fb = &rulerbuf;
	bar.p.fb->nline = 1;
	bar.p.l = bar.draw = lineof(bar.p.fb->lines.beg);
	refreshw(&bar);

	if (!entry)
		cmd_edit(0, NULL);
	else
		cmd_edit(2, (const char*[]){"e", entry});
}

void
jumping(void)
{
	mark(&ARG(.i = '\''));
	has_sel = ctab->w->p.l;
}

void
keypress(int k)
{
	char *buf;
	int h;

	if (k == 0)
		return;

	h = skb_handle_key(k);
	if (!h && !mode_can_insert()) {
		skb_ncombo = 0;
		return;
	}
	if (h)
		return;

	buf = sbuf;
	for (int i = 0; i < skb_ncombo; i++) {
		*buf = skb_combo[i];
		buf++;
	}
	*buf = '\0';
	skb_ncombo = 0;

	insert(&ARG(.s = sbuf));
}

int
match(const char *str)
{
	int r;
	if (!pattern)
		return 0;

	matched = NULL;

	r = !regexec(pattern, str, MAX_MACHES, matches, 0);
	if (r) {
		set_col(ctab->w, matches[0].rm_so);
		SEL_MARKER = ctab->w->p;
		set_col(ctab->w, matches[0].rm_eo);
		matched = has_sel = ctab->w->p.l;
	}

	return r;
}

int
mode_can_insert(void)
{
	switch (cmode) {
	case MODE_INS:
	case MODE_CMD:
	case MODE_SEARCH:
		return 1;
	}
	return 0;
}

struct undo *
new_undo(struct fbuf *fb)
{
	struct undo *u;

	if (fb->undo.end && fb->undo.end->nex) {
		fb->undo.end = fb->undo.end->nex;
		u = undoof(fb->undo.end);
		str_free(&u->e.replace);
		return u;
	}


	u = ecalloc(1, sizeof(*u));
	list_insert(&fb->undo, fb->undo.end, &u->link);
	return u;
}

void
new_line(const union arg *arg)
{
	struct edit e;
	e.beg = ctab->w->p;
	e.beg.col = ctab->w->p.l->s.len - 1;
	e.end = e.beg;
	estr_from_cstr(&e.replace, "\n");
	edit(NULL, &e);
	str_free(&e.replace);
	has_sel = NULL;
}

void
paste(const union arg *arg)
{
	char **reg = get_reg(arg->s[0]);
	if (arg->s[0] == '+') {
		if (*reg)
			free(*reg);
		*reg = sys_paste();
	}
	if (!reg || !*reg)
		return;
	if (!arg->s[1])
		move_col(&ARG(.i = 1));
	insert(&ARG(.s = *reg));
}

void
redo(const union arg *arg)
{
	struct edit e;
	struct fbuf *fb = ctab->w->p.fb;
	struct undo *u;
	if (!fb->undo.end->nex)
		return;
	u = undoof(fb->undo.end->nex);
	e = u->e;
	str_empty(&u->e.replace);
	edit(NULL, &e);
}

void
refreshl(struct win *w, struct line *l)
{
	*l->r = 0;
	refreshw(w);
}

void
remove_fbuf(struct fbuf *fb)
{
	for (int i = 0; i < fbs.n; i++) {
		if (fbs.e[i] == fb) {
			darr_remove(&fbs, i);
			goto clean;
		}
	}
	return;

clean:
	list_for_each(struct line, l, fb->lines.beg, nex, link) {
		str_free(&l->s);
		free(l);
	}

	free(fb);
}

void
remove_line(struct fbuf *fb, struct line *l)
{
	fb->ldirty = 1;
	fb->nline--;
	list_remove(&fb->lines, &l->link);
	str_free(&l->s);
	free(l);
}

void
render_line(const struct win *w, struct line *l)
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

int
request_key(void)
{
	draw();

	if (poll(fds, 1, -1) == -1 && errno != EINTR)
		die("poll()");
	if (!(fds[0].revents & POLLIN))
		return 0;
	return sctui_grab_key();
}

void
ruler(void)
{
	struct line *l;
	int len, padding;

	l = lineof(rulerbuf.lines.beg);

	estr_clean(&l->s);
	if (ctab->w == &bar)
		return;

	len = snprintf(sbuf, BUFSIZ, "%d,%d", ctab->w->p.row, ctab->w->p.col);

	padding = global_sctui.w - len - skb_ncombo - 4;
	if (mode_str[cmode]) {
		padding -= strlen(mode_str[cmode]);
		estr_append_cstr(&l->s, mode_str[cmode]);
	}

	for (int i = 0; i < padding; i++)
		estr_append_chr(&l->s, ' ');

	for (int i = 0; i < skb_ncombo; i++)
		estr_append_chr(&l->s, skb_combo[i]);
	estr_append_cstr(&l->s, "    ");

	estr_append_cstr(&l->s, sbuf);

	refreshl(&bar, l);
}

int
search_nex(void)
{
	if (matched) {
		if (ctab->w->p.row >= ctab->w->p.fb->nline - 1)
			return 0;
		move_row(&ARG(.i = 1));
	}
	do {
		if (match(ctab->w->p.l->s.s))
			return 1;
		if (ctab->w->p.row >= ctab->w->p.fb->nline - 1)
			break;
		move_row(&ARG(.i = 1));
	} while (1);

	return 0;
}

int
search_prv(void)
{
	if (matched) {
		if (ctab->w->p.row <= 0)
			return 0;
		move_row(&ARG(.i = -1));
	}

	do {
		if (match(ctab->w->p.l->s.s))
			return 1;
		if (ctab->w->p.row <= 0)
			break;
		move_row(&ARG(.i = -1));
	} while (1);

	return 0;
}

void
sel_word_nex(const char **beg, const char **end)
{
	const char *b = *beg, *e = *end;
	while (*b && !isalpha(*b))
		b++;
	e = b;

	while (*e && (isalpha(*e) || *e == '_'))
		e++;
	do {
		if (b == ctab->w->p.l->s.s)
			break;
		b--;
		if (!isalpha(*b) && *b != '_') {
			b++;
			break;
		}
	} while (1);
	*beg = b;
	*end = e;
}

void
sel_word_prv(const char **beg, const char **end)
{
	const char *b = *beg, *e = *beg;
	int skip = 0;

	if (isalpha(*e) || *e == '_')
		skip = 1;

	do {
		if (e == ctab->w->p.l->s.s)
			break;
		e--;
		if (skip && (isalpha(*e) || *e == '_'))
			continue;
		skip = 0;
		if (isalpha(*e) || *e == '_') {
			e++;
			break;
		}
	} while (1);

	b = e;
	do {
		if (b == ctab->w->p.l->s.s)
			break;
		b--;
		if (!isalpha(*b) && *b != '_') {
			b++;
			break;
		}
	} while (1);

	*beg = e;
	*end = b;
}

void
set_bar_buf(struct fbuf *fb)
{
	bar.p.fb = fb;
	bar.p.l = bar.draw = lineof(fb->lines.beg);
	set_col(&bar, 0);
	refreshw(&bar);
}

void
set_child(void)
{
	struct sigaction sa;
	setsid();
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, NULL);
}

void
set_col(struct win *w, int col)
{
	int max = 0;
	if (ctab->w->p.l)
		max = ctab->w->p.l->s.len - 1;
	ctab->w->p.col = align(col, 0, max);
	ctab->w->p.fb->pos.col = ctab->w->p.col;
	refreshw(ctab->w);
}

void
set_row(struct win *w, int row)
{
	int orig = w->p.row;

	w->p.row = align(row, 0, w->p.fb->nline - 1);
	if (w->p.row <= w->p.rowoff)
		w->p.rowoff = w->p.row;
	else if (w->p.row >= w->p.rowoff + w->h)
		w->p.rowoff = w->p.row - w->h + 1;
	w->p.fb->pos.row = w->p.row;
	w->p.fb->pos.rowoff = w->p.rowoff;

	refreshw(w);

	if (w->p.row == 0) {
		w->p.l = lineof(w->p.fb->lines.beg);
	} else if (w->p.row == w->p.fb->nline - 1) {
		w->p.l = lineof(w->p.fb->lines.end);
	} else if (w->p.row > orig) {
		w->p.l = get_line_nex(w->p.l, w->p.row - orig);
	} else if (w->p.row < orig) {
		w->p.l = get_line_prv(w->p.l, orig - w->p.row);
	}
	get_draw_line(w);

	w->p.fb->pos.l = w->p.l;

	set_col(w, w->p.col);
}

void
sys_copy(const char *s)
{
	const char **cmd = NULL;
	int fds[2];

	if (!(cmd = sys_copy_cmd()))
		return;

	if (pipe(fds) < 0)
		die("pipe()");

	if (fork() == 0) {
		set_child();
		close(fds[1]);
		if (dup2(fds[0], STDIN_FILENO) < 0)
			die("dup2()");
		close(fds[0]);
		execvp(cmd[0], (char**)cmd);
		die("execvp()");
	}

	close(fds[0]);
	write(fds[1], s, strlen(s));
	close(fds[1]);
}

char *
sys_paste(void)
{
	const char **cmd = NULL;
	int fds[2], r;
	struct str result;

	if (!(cmd = sys_paste_cmd()))
		return NULL;

	if (pipe(fds) < 0)
		die("pipe()");

	if (fork() == 0) {
		set_child();
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			die("dup2()");
		close(fds[1]);
		execvp(cmd[0], (char**)cmd);
		die("execvp()");
	}

	str_empty(&result);
	close(fds[1]);
	while ((r = read(fds[0], sbuf, sizeof(sbuf) - 1)))
		estr_append_str(&result, &STR(sbuf, r));
	close(fds[0]);

	return result.s;
}

struct fbuf *
tmp_fbuf(void)
{
	struct fbuf *fb = ecalloc(1 ,sizeof(*fb));

	darr_append(&fbs, fb);
	list_init(&fb->lines);

	strcpy(fb->path, "<tmp>");
	fb->tmp = 1;
	fb->pos.fb = fb;

	return fb;
}

void
xfocus_win(struct win *w)
{
	if (w == &ctab->tmpw && !ctab->enable_tmpw) {
		toggle_tmp_win(0);
		return;
	}
	w->prv = ctab->w;
	ctab->w = w;
	refreshw(ctab->w);
}

void
xgoto_mark(struct marker *m)
{
	ctab->w->p.fb = m->fb;
	if (ctab->w->p.fb->ldirty || !m->l)
		m->l = get_line_nex(lineof(m->fb->lines.beg), m->row);
	ctab->w->p.l = m->l;
	ctab->w->p.row = m->row;
	ctab->w->p.rowoff = m->rowoff;
	set_row(ctab->w, m->row);
	set_col(ctab->w, m->col);
}

/* key functions */
void
concat_line(const union arg *arg)
{
	struct edit e;
	e.beg = e.end = ctab->w->p;
	e.end.l = lineof(e.end.l->link.nex);
	e.end.row++;
	e.end.col = 0;
	str_empty(&e.replace);
	edit(NULL, &e);
}

void
backspace(const union arg *arg)
{
	struct edit e;
	int pos = ctab->w->p.col - arg->i;
	e.beg = e.end = ctab->w->p;
	if (pos < 0) {
		e.beg.l = lineof(e.beg.l->link.prv);
		e.beg.row--;
		e.beg.col = e.beg.l->s.len - 1;
	} else {
		e.beg.col--;
	}
	str_empty(&e.replace);
	edit(NULL, &e);
}

void
change(const union arg *arg)
{
	delete(arg);
	mode(&ARG(.i = MODE_INS));
}

void
cmd(const union arg *arg)
{
	int m = cmode;
	char *tok, *dup, *saver;

	darr(char*) args;
	darr_init(&args);

	if (arg->s) {
		dup = strdup(arg->s);
	} else {
		dup = strdup(bar.p.l->s.s);
		if (dup[bar.p.l->s.len - 1] == '\n')
			dup[bar.p.l->s.len - 1] = '\0';
		bar.p.l->s.s[0] = '\n';
		bar.p.l->s.s[1] = '\0';
		bar.p.l->s.len = 1;
		refreshl(&bar, bar.p.l);
	}

	mode(&ARG(.i = MODE_NOR));

	switch (m) {
	case MODE_SEARCH:
		comp_pattern(dup, 0);
		match(ctab->w->p.l->s.s);
		break;
	default:
		for (tok = dup; ; tok = NULL) {
			if (!(tok = strtok_r(tok, " \t\n", &saver)))
				break;
			darr_append(&args, tok);
		}
		darr_append(&args, NULL);
		args.n--;
		break;
	}

	if (!args.n || !args.e[0])
		goto end;

	for (int i = 0; cmds[i].cmd != NULL; i++) {
		if (strcmp(cmds[i].cmd, args.e[0]) == 0 ||
		    strcmp(cmds[i].alias, args.e[0]) == 0) {
			cmds[i].func(args.n, (const char**)args.e);
			break;
		}
	}

end:
	free(dup);
}

void
delete(const union arg *arg)
{
	struct str _buf, *buf = NULL;
	struct edit e;

	if (!has_sel && ctab->w->p.col >= (int)ctab->w->p.l->s.len - 1) {
		concat_line(0);
		return;
	}

	e.beg = ctab->w->p;
	if (has_sel) {
		e.end = SEL_MARKER;
		buf = &_buf;
	} else {
		e.end = e.beg;
		e.end.col++;
	}
	str_empty(&e.replace);
	edit(buf, &e);

	if (buf)
		dup_to_reg('+', buf->s);

	has_sel = NULL;
}

void
find_nex(const union arg *arg)
{
	struct marker *p = &ctab->w->p;
	const char *c = p->l->s.s + p->col;
	int k = request_key();

	jumping();
	while (*c) {
		if (*c == '\n') {
			if (arg->i) {
				move_row(&ARG(.i = 1));
				c = p->l->s.s;
				continue;
			} else {
				return;
			}
		}
		if (*c == k)
			break;
		c++;
	}

	c++; /* let the selection include expected character */
	has_sel = p->l;
	set_col(ctab->w, c - p->l->s.s);
}

void
find_prv(const union arg *arg)
{
	struct marker *p = &ctab->w->p;
	const char *c = p->l->s.s + p->col;
	int k = request_key();

	jumping();
	while (*c) {
		if (*c == k)
			break;
		if (c == p->l->s.s) {
			if (arg->i) {
				move_row(&ARG(.i = - 1));
				c = p->l->s.s + p->l->s.len - 1;
				continue;
			} else {
				return;
			}
		}
		c--;
	}

	has_sel = p->l;
	set_col(ctab->w, c - p->l->s.s);
}

void
focus_win(const union arg *arg)
{
	struct win *w;
	switch (arg->i) {
	case FOCUS_PRV:
		if (!ctab->w->prv)
			return;
		w = ctab->w->prv;
		break;
	case FOCUS_MAIN: w = &ctab->mw;    break;
	case FOCUS_TMP:  w = &ctab->tmpw;  break;
	}
	xfocus_win(w);
}

void
goto_beg(const union arg *arg)
{
	switch (arg->i) {
	case GOTO_IN_FILE:
		set_row(ctab->w, 0);
		break;
	case GOTO_IN_LINE:
		jumping();
		set_col(ctab->w, 0);
		break;
	}
}

void
goto_end(const union arg *arg)
{
	switch (arg->i) {
	case GOTO_IN_FILE:
		set_row(ctab->w, ctab->w->p.fb->nline - 1);
		break;
	case GOTO_IN_LINE:
		jumping();
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
		break;
	}
}

void
goto_mark(const union arg *arg)
{
	int k;
	struct marker *m;

	if (arg->i == 0)
		k = request_key();
	else
		k = arg->i;

	m = get_marker(k);
	if (!m)
		return;
	if (!m->fb)
		return;

	xgoto_mark(m);
}

void
insert(const union arg *arg)
{
	struct edit e;
	e.beg = e.end = ctab->w->p;
	estr_from_cstr(&e.replace, arg->s);
	edit(NULL, &e);
	str_free(&e.replace);
	has_sel = NULL;
}

void
mark(const union arg *arg)
{
	int k;
	struct marker *m;

	if (arg->i == 0)
		k = request_key();
	else
		k = arg->i;

	m = get_marker(k);
	if (!m)
		return;

	*m = ctab->w->p;
}

void
mode(const union arg *arg)
{
	int orig = cmode;
	cmode = arg->i;
	switch (cmode) {
	case MODE_CMD:
	case MODE_SEARCH:
		has_sel = NULL;
		set_bar_buf(&cmdbuf);
		cmdback = ctab->w;
		ctab->w = &bar;
		break;
	default:
		if (orig == MODE_CMD || orig == MODE_SEARCH) {
			set_bar_buf(&rulerbuf);
			ctab->w = cmdback;
		}
		break;
	}
}

void
move_col(const union arg *arg)
{
	set_col(ctab->w, ctab->w->p.col + arg->i);
	has_sel = NULL;
}

void
move_row(const union arg *arg)
{
	set_row(ctab->w, ctab->w->p.row + arg->i);
	has_sel = NULL;
}

void
search(const union arg *arg)
{
	int (*fn)(void) = search_nex;
	struct marker orig;

	if (!pattern)
		return;

	orig = ctab->w->p;

	/* TODO: search in same line */
	if (arg->i == -1)
		fn = search_prv;
	if (fn())
		return;

	if (arg->i == -1) {
		goto_end(&ARG(.i = GOTO_IN_FILE));
	} else {
		goto_beg(&ARG(.i = GOTO_IN_FILE));
	}

	matched = NULL;

	if (fn())
		return;

	xgoto_mark(&orig);
	has_sel = NULL;
}

void
sel(const union arg *arg)
{
	has_sel = SEL_MARKER.l;
}

void
sel_line(const union arg *arg)
{
	if (arg->i > 0) {
		set_col(ctab->w, 0);
		jumping();
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
	} else {
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
		jumping();
		set_col(ctab->w, 0);
	}
}

void
sel_word(const union arg *arg)
{
	const char *beg, *end, *t;
	struct line *l;

	l = ctab->w->p.l;
	beg = l->s.s + ctab->w->p.col;

	if (arg->i > 0)
		sel_word_nex(&beg, &end);
	else
		sel_word_prv(&beg, &end);

	set_col(ctab->w, beg - l->s.s);
	jumping();

	set_col(ctab->w, end - l->s.s);

	refreshw(ctab->w);

	if (beg > end) {
		t = beg;
		beg = end;
		end = t;
	}
	comp_pattern(beg, end - beg);
	matched = has_sel;
}

void
suspend(const union arg *arg)
{
	sctui_fini();
	sctui_close_alt_screen();
	sctui_commit();
	kill(0, SIGSTOP);
	refreshw(&ctab->mw);
	if (ctab->enable_tmpw)
		refreshw(&ctab->tmpw);
	sctui_init();
	sctui_open_alt_screen();
	sctui_commit();
}

void
toggle_tmp_win(const union arg *arg)
{
	struct win *mw, *w;

	mw = &ctab->mw;
	w = &ctab->tmpw;

	if (ctab->w == &ctab->tmpw || ctab->enable_tmpw) {
		mw->h += w->h;
		ctab->enable_tmpw = 0;
		xfocus_win(mw);
		return;
	}

	memcpy(w, mw, sizeof(*w));

	w->w = mw->w;
	w->h = mw->h / 2;
	mw->h = w->h + mw->h % 2;
	w->y = mw->y + mw->h;

	set_row(w, w->p.row);
	set_row(mw, mw->p.row);

	ctab->enable_tmpw = 1;

	/* it will call toggle_tmp_win(),
	 * so ctab->enable_tmp must be set before */
	xfocus_win(w);
}

void
undo(const union arg *arg)
{
	struct edit e;
	struct fbuf *fb = ctab->w->p.fb;
	struct undo *u = undoof(fb->undo.end);

	if (fb->undo.end->prv)
		fb->undo.end = fb->undo.end->prv;

	e = u->e;
	str_empty(&u->e.replace);

	edit(NULL, &e);

	fb->undo.end = fb->undo.end->prv;
}

void
yank(const union arg *arg)
{
	struct str buf;
	struct line *l;
	struct selection sel;
	str_empty(&buf);

	sel.beg = &SEL_MARKER;
	sel.end = &ctab->w->p;
	get_sel(&sel);
	l = sel.beg->l;
	if (sel.first_len)
		estr_append_str(&buf, &STR(l->s.s + sel.beg->col, sel.first_len));

	l = lineof(l->link.nex);
	for (int i = sel.beg->row + 1; i < sel.end->row; i++) {
		if (l->s.len <= 0)
			continue;
		estr_append_str(&buf, &l->s);
		l = lineof(l->link.nex);
	}

	if (sel.last_len)
		estr_append_str(&buf, &STR(l->s.s, sel.last_len));

	dup_to_reg('+', buf.s);
}

/* command functions */
void
cmd_buffer(int argc, const char *argv[])
{
	struct fbuf *fb;
	int idx, width;
	struct line *l;

	if (argc > 1 && argv[1]) {
		if ((idx = atoi(argv[1])) > fbs.n - 1)
			return;
		if (idx < 0)
			return;
		fb = fbs.e[idx];
		fb->pos.fb = fb;
		xgoto_mark(&fb->pos);
		return;
	}

	xfocus_win(&ctab->tmpw);

	fb = tmp_fbuf();

	width = snprintf(sbuf, sizeof(sbuf), "%d", fbs.n - 1);

	/* tmp_fbuf will append the [fb] to [fbs], so don't handle it */
	for (int i = 0; i < fbs.n - 1; i++, fb->nline++) {
		l = ecalloc(1, sizeof(*l));
		str_empty(&l->s);
		estr_expand_siz(&l->s, width + strlen(fbs.e[i]->path) + 16);
		l->s.len = snprintf(l->s.s, l->s.siz, "%-*d\"%s\"\n",
				width + 2, i, fbs.e[i]->path);
		list_insert(&fb->lines, fb->lines.end, &l->link);
	}

	xgoto_mark(&fb->pos);
}

void
cmd_edit(int argc, const char *argv[])
{
	struct fbuf *fb;
	FILE *fp;
	struct line *l;
	int nline;

	if (argc > 1 && argv) {
		for (int i = 0; i < fbs.n; i++) {
			if (strcmp(fbs.e[i]->path, argv[1]) == 0) {
				fb = fbs.e[i];
				goto setwin;
			}
		}
	}

	fb = ecalloc(1, sizeof(*fb));
	darr_append(&fbs, fb);

	if (argc <= 1) {
		strcpy(fb->path, "<unnamed>");
	} else {
		strcpy(fb->path, argv[1]);
	}

	if (argc <= 1 || !(fp = fopen(argv[1], "r"))) {
		empty_fbuf(fb);
		goto setwin;
	}

	list_init(&fb->lines);
	list_init(&fb->undo);
	for (nline = 0; fgets(sbuf, BUFSIZ, fp); nline++) {
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->s, sbuf);
		list_insert(&fb->lines, fb->lines.end, &l->link);
	}
	fb->nline = nline;

	fclose(fp);
setwin:
	fb->pos.fb = fb;
	xgoto_mark(&fb->pos);
}

void
cmd_marks(int argc, const char *argv[])
{
	struct fbuf *fb;
	struct line *l;

	xfocus_win(&ctab->tmpw);

	fb = tmp_fbuf();

	/* tmp_fbuf will append the [fb] to [fbs], so don't handle it */
	for (int i = 0; i < (int)LENGTH(markers); i++, fb->nline++) {
		if (!markers[i].fb) {
			fb->nline--;
			continue;
		}
		l = ecalloc(1, sizeof(*l));
		str_empty(&l->s);
		estr_expand_siz(&l->s, strlen(markers[i].fb->path) + 64);
		l->s.len = snprintf(l->s.s, l->s.siz, "%c \"%s\":%d,%d\n",
				get_marker_chr(i),
				markers[i].fb->path,
				markers[i].row, markers[i].col);
		list_insert(&fb->lines, fb->lines.end, &l->link);
	}

	xgoto_mark(&fb->pos);

	if (fb->nline == 0)
		cmd_quit(0, NULL);
}

void
cmd_write(int argc, const char *argv[])
{
	FILE *fp;
	const char *path;

	if (argc <= 1 || !argv[1])
		path = ctab->w->p.fb->path;
	else
		path = argv[1];

	if (!(fp = fopen(path, "w")))
		return;

	list_for_each(struct line, l, ctab->w->p.fb->lines.beg, tmp, link)
		fputs(l->s.s, fp);

	fclose(fp);
}

void
cmd_quit(int argc, const char *argv[])
{
	struct fbuf *fb;
	int using = 0;

	if (ctab->w == &ctab->mw) {
		running = 0;
		return;
	}

	toggle_tmp_win(0);

	fb = ctab->tmpw.p.fb;
	if (!fb->tmp)
		return;

	for (int i = 0; i < tabs.n; i++) {
		using += tabs.e[i]->mw.p.fb == fb;
		using += tabs.e[i]->tmpw.p.fb == fb;
	}

	if (using > 1)
		return;

	remove_fbuf(fb);
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
	while (running)
		keypress(request_key());
	fini();

	return 0;
}

#define SKB_IMPL
#include "skb.h"
