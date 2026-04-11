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
#define refreshw(WREF) ((WREF)->refresh = 1)

static void comp_pattern(const char *p, int len);
static void copy(const char *s);
static void draw(void);
static void draw_sel(void);
static void draw_win(struct win *w);
static void dup_to_reg(int r, const char *s, int len);
static void empty_fbuf(struct fbuf *fb);
static struct line *empty_line(void);
static void fini(void);
static void get_draw_line(struct win *w);
static const struct key *get_keys_table(void);
static struct line *get_line_nex(struct line *beg, int step);
static struct line *get_line_prv(struct line *beg, int step);
static struct marker *get_marker(int k);
static char **get_reg(int k);
static void get_rowcol(struct marker *m);
static int get_rx(struct line *l, int col);
static int get_ry(int row);
static void init(void);
static void jump_done(void);
static void jump_start(void);
static void keypress(int k);
static int match(const char *str);
static int mode_can_insert(void);
static void refreshl(struct win *w, struct line *l);
static void remove_line(struct fbuf *fb, struct line *l);
static void render_line(const struct win *w, struct line *l);
static int request_key(void);
static void ruler(void);
static int search_nex(void);
static int search_prv(void);
static void sel_word_nex(const char **beg, const char **end);
static void sel_word_prv(const char **beg, const char **end);
static void set_bar_buf(struct fbuf *fb);
static void set_col(struct win *w, int col);
static void set_row(struct win *w, int row);
static void set_rowcol(struct marker *m);

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

typedef darr(struct fbuf) fb_arr;
typedef darr(struct tab) tab_arr;
typedef darr(struct win) win_arr;
static fb_arr  fbs;
static tab_arr tabs;
static win_arr wins;

static struct win bar;
static struct win *cmdback;
static struct fbuf cmdbuf;
static struct fbuf rulerbuf;

/* '+' */
static char *regs[1];

/* state */
typedef darr(struct cursor) cursor_arr;
static int        cursor;
static cursor_arr cursors;

static int         cmode = MODE_NOR;
static struct tab *ctab;

static struct line *has_sel;

/* numbers + lowers + suppers + '\'' */
static struct marker markers[10 + 52 + 1];
#define SEL_MARKER markers[10 + 52]

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
copy(const char *s)
{
	const char **cmd = NULL;
	int fds[2];
	struct sigaction sa;

	if (!(cmd = copy_cmd()))
		return;

	if (pipe(fds) < 0)
		die("pipe()");

	if (fork() == 0) {
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

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

void
draw(void)
{
	for (int i = 0; i < wins.n; i++)
		draw_win(&wins.e[i]);

	ruler();
	draw_win(&bar);

	if (has_sel)
		draw_sel();
	sctui_move(get_rx(ctab->w->p.l, ctab->w->p.col),
			get_ry(ctab->w->p.row));

	sctui_commit();
}

void
draw_sel(void)
{
	int rx, ry, len;

	sctui_out(sctui_attr_on(sel_attr), 0);

	for (int i = 0; i < cursors.n; i++) {
		if (!cursors.e[i].l || !cursors.e[i].sel)
			continue;
		rx = get_rx(cursors.e[i].l, cursors.e[i].col);
		ry = get_ry(cursors.e[i].row);
		len = get_rx(cursors.e[i].l, cursors.e[i].col + cursors.e[i].sel) - rx;
		sctui_text(rx, ry, cursors.e[i].l->r + rx, MIN(len, ctab->w->w));
	}

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
dup_to_reg(int r, const char *s, int len)
{
	char **reg = get_reg(r);
	if (!reg)
		return;
	if (*reg)
		free(*reg);
	*reg = strndup(s, len);

	if (r == '+')
		copy(*reg);
}

void
empty_fbuf(struct fbuf *fb)
{
	struct line *l = empty_line();
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
		k = k - 'a' + '9';
	} else if (k >= 'A' && k <= 'Z') {
		k = k - 'A' + '9' + 26;
	} else if (k == '\'') {
		k = &SEL_MARKER - markers;
	} else {
		return NULL;
	}
	return &markers[k];
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

void
get_rowcol(struct marker *m)
{
	m->fb = ctab->w->p.fb;
	m->l = ctab->w->p.l;
	m->row = ctab->w->p.row;
	m->rowoff = ctab->w->p.rowoff;
	m->col = ctab->w->p.col;
}

int
get_rx(struct line *l, int col)
{
	int rx = 0;
	for (int i = 0; i < col; i++) {
		switch (ctab->w->p.l->s.s[i]) {
		case '\t':
			rx += strlen(tab_render);
			break;
		default:
			rx++;
			break;
		}
	}
	return ctab->w->x + rx;
}

int
get_ry(int row)
{
	return ctab->w->y + row - ctab->w->p.rowoff;
}

void
init(void)
{
	struct line *l;

	sctui_init();
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

	darr_init(&tabs);
	darr_expand(&tabs)
	darr_init(&wins);
	darr_expand(&wins)

	ctab = tabs.e;
	ctab->w = wins.e;
	ctab->w->w = global_sctui.w;
	ctab->w->h = global_sctui.h - 1;

	l = empty_line();
	list_init(&rulerbuf.lines);
	list_insert(&rulerbuf.lines, rulerbuf.lines.end, &l->link);

	l = empty_line();
	list_init(&cmdbuf.lines);
	list_insert(&cmdbuf.lines, cmdbuf.lines.end, &l->link);

	memset(&bar, 0, sizeof(bar));
	bar.x = 0;
	bar.y = global_sctui.h - 1;
	bar.w = global_sctui.w;
	bar.h = 1;
	bar.p.fb = &rulerbuf;
	bar.p.fb->nline = 1;
	bar.p.l = bar.draw = lineof(bar.p.fb->lines.beg);
	refreshw(&bar);

	darr_init(&cursors);
	darr_expand(&cursors);
	memset(cursors.e, 0, sizeof(*cursors.e));
	cursor = 0;

	if (!entry)
		cmd_edit(0, NULL);
	else
		cmd_edit(2, (const char*[]){"e", entry});
}

void
jump_done(void)
{
	if (ctab->w->p.col > cursors.e[cursor].col) {
		cursors.e[cursor].sel = ctab->w->p.col - cursors.e[cursor].col;
	} else {
		cursors.e[cursor].sel = cursors.e[cursor].col - ctab->w->p.col;
		set_col(ctab->w, ctab->w->p.col);
		cursors.e[cursor].col = ctab->w->p.col;
	}
}

void
jump_start(void)
{
	mark(&ARG(.i = '\''));
	cursors.e[cursor].row = ctab->w->p.row;
	cursors.e[cursor].col = ctab->w->p.col;
	cursors.e[cursor].l = ctab->w->p.l;
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
	if (!h && !mode_can_insert())
		skb_ncombo = 0;
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
		get_rowcol(&SEL_MARKER);
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

void
new_line(const union arg *arg)
{
	struct line *l, *prv = ctab->w->p.l;
	struct str s;

	ctab->w->p.fb->ldirty = 1;

	l = ecalloc(1, sizeof(*l));
	if (arg->s[0] == 'u')
		prv = lineof(prv->link.prv);
	list_insert(&ctab->w->p.fb->lines,
			prv ? &prv->link : NULL,
			&l->link);
	ctab->w->p.fb->nline++;
	ctab->w->p.l = l;

	if (arg->s[1] && prv) {
		s.s = prv->s.s + ctab->w->p.col;
		s.len = prv->s.len - ctab->w->p.col;
		s.siz = s.len + 1;
		if (s.len > 0)
			estr_append_str(&l->s, &s);
		estr_remove(&prv->s, ctab->w->p.col, s.len - 1);
	} else {
		estr_from_cstr(&l->s, "\n");
	}

	set_col(ctab->w, 0);
	move_row(&ARG(.i = arg->s[0] == 'd'));

	if (prv)
		refreshl(ctab->w, prv);
	refreshl(ctab->w, l);
}

void
paste(const union arg *arg)
{
	char **reg = get_reg(arg->s[0]);
	if (!reg || !*reg)
		return;
	if (!arg->s[1])
		move_col(&ARG(.i = 1));
	insert(&ARG(.s = *reg));
}

void
refreshl(struct win *w, struct line *l)
{
	*l->r = 0;
	refreshw(w);
}

void
remove_line(struct fbuf *fb, struct line *l)
{
	fb->ldirty = 1;
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
		move_row(&ARG(.i = 1));
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
set_col(struct win *w, int col)
{
	ctab->w->p.col = align(col, 0, ctab->w->p.l->s.len - 1);
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
set_rowcol(struct marker *m)
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
	struct line *l = ctab->w->p.l, *prv;

	ctab->w->p.fb->ldirty = 1;

	if (arg->i == -1) {
		prv = lineof(l->link.prv);
		if (!prv)
			return;
		ctab->w->p.col = prv->s.len;
		move_row(&ARG(.i = -1));
	} else {
		prv = l;
		l = lineof(l->link.nex);
		if (!l)
			return;
	}

	prv->s.len -= 1; /* remove the '\n' */
	estr_append_str(&prv->s, &l->s);

	remove_line(ctab->w->p.fb, l);

	refreshl(ctab->w, prv);
}

void
backspace(const union arg *arg)
{
	struct line *l = ctab->w->p.l;
	int pos = ctab->w->p.col - arg->i;
	if (pos < 0) {
		concat_line(&ARG(.i = -1));
		return;
	}
	estr_remove(&l->s, pos, arg->i);
	refreshl(ctab->w, l);
	move_col(&ARG(.i = -arg->i));
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
	case MODE_CMD:
		for (tok = dup; ; tok = NULL) {
			if (!(tok = strtok_r(tok, " \t\n", &saver)))
				break;
			darr_append(&args, tok);
		}
		darr_append(&args, NULL);
		args.n--;
		break;
	case MODE_SEARCH:
		comp_pattern(dup, 0);
		match(ctab->w->p.l->s.s);
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
	struct str buf;
	struct cursor *c;
	int len;

	if (!has_sel) {
		if (ctab->w->p.col >= (int)ctab->w->p.l->s.len - 1)
			concat_line(&ARG(0));
		else
			estr_remove(&ctab->w->p.l->s, ctab->w->p.col, 1);
		refreshl(ctab->w, ctab->w->p.l);
		return;
	}

	str_empty(&buf);

	c = cursors.e;
	set_row(ctab->w, c->row);
	for (int i = 0, n = cursors.n, concat = 0; i < n;
			i++, c++, concat = 0) {
		len = ctab->w->p.l->s.len;
		if (c->sel == 0)
			continue;
		if (c->col + c->sel >= len) {
			c->sel -= 1;
			concat = 1;
		}
		estr_append_str(&buf, &STR(
					ctab->w->p.l->s.s + c->col,
					c->sel + concat));
		estr_remove(&ctab->w->p.l->s, c->col, c->sel);
		if (concat) {
			cursors.e[i + 1].col += ctab->w->p.l->s.len - 1;
			concat_line(&ARG(0));
			cursors.e[i + 1].l = ctab->w->p.l;
		}
		refreshl(ctab->w, ctab->w->p.l);
	}

	set_row(ctab->w, cursors.e[0].row);
	set_col(ctab->w, cursors.e[0].col);

	dup_to_reg('+', buf.s, buf.len);
	str_free(&buf);

	has_sel = NULL;
}

void
goto_beg(const union arg *arg)
{
	switch (arg->i) {
	case GOTO_IN_FILE:
		set_row(ctab->w, 0);
		break;
	case GOTO_IN_LINE:
		jump_start();
		set_col(ctab->w, 0);
		jump_done();
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
		jump_start();
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
		jump_done();
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

	set_rowcol(m);
}

void
insert(const union arg *arg)
{
	const char *c;
	struct line *l = ctab->w->p.l;
	struct str s;

	s.s = (char*)arg->s;
	s.len = 0;
	for (c = arg->s; *c; c++) {
		if (*c == '\n') {
			estr_insert_str(&l->s, ctab->w->p.col - s.len, &s);
			s.s = (char*)(c + 1);
			s.siz = s.len = 0;
			new_line(&ARG(.s = "dI")); /* keep the text of line */
			continue;
		}
		s.len++;
		ctab->w->p.col++;
	}

	refreshl(ctab->w, l);

	estr_insert_str(&l->s, ctab->w->p.col - s.len, &s);
	set_col(ctab->w, ctab->w->p.col);
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

	get_rowcol(m);
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
	cursors.e[cursor].sel = 0;
	has_sel = NULL;
}

void
quit(const union arg *arg)
{
	running = 0;
}

void
search(const union arg *arg)
{
	int (*fn)(void) = search_nex;
	struct marker orig;

	if (!pattern)
		return;

	get_rowcol(&orig);

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

	set_rowcol(&orig);
	has_sel = NULL;
}

void
sel(const union arg *arg)
{
	int beg = MIN(SEL_MARKER.row, ctab->w->p.row),
	    end = MAX(SEL_MARKER.row, ctab->w->p.row);
	struct line *l;
	struct marker *beg_marker, *end_marker;

	if (SEL_MARKER.row == ctab->w->p.row) {
		beg = MIN(SEL_MARKER.col, ctab->w->p.col);
		end = MAX(SEL_MARKER.col, ctab->w->p.col);
		darr_resize(&cursors, 1);
		cursor = 0;
		cursors.e[cursor].row = ctab->w->p.row;
		cursors.e[cursor].col = beg;
		cursors.e[cursor].sel = end - beg;
		cursors.e[cursor].l = ctab->w->p.l;
		has_sel = ctab->w->p.l;
		return;
	}

	darr_resize(&cursors, end - beg + 1);
	cursor = 0;
	end = end - beg;
	beg = 0;

	if (SEL_MARKER.row < ctab->w->p.row) {
		beg_marker = &SEL_MARKER;
		end_marker = &ctab->w->p;
		cursor = end;
	} else {
		beg_marker = &ctab->w->p;
		end_marker = &SEL_MARKER;
	}

	cursors.e[beg].row = beg_marker->row;
	cursors.e[beg].col = beg_marker->col;
	cursors.e[beg].sel = beg_marker->l->s.len - beg_marker->col;
	cursors.e[beg].l   = beg_marker->l;
	cursors.e[end].row = end_marker->row;
	cursors.e[end].col = 0;
	cursors.e[end].sel = end_marker->col;
	cursors.e[end].l   = end_marker->l;

	l = lineof(beg_marker->l->link.nex);
	for (int i = beg + 1, j = 1; l && i < end; i++, j++) {
		cursors.e[j].col = 0;
		cursors.e[j].row = i;
		cursors.e[j].sel = l->s.len;
		cursors.e[j].l = l;
		l = lineof(l->link.nex);
	}

	has_sel = SEL_MARKER.l;
}

void
sel_line(const union arg *arg)
{
	if (arg->i > 0) {
		set_col(ctab->w, 0);
		jump_start();
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
		jump_done();
	} else {
		set_col(ctab->w, ctab->w->p.l->s.len - 1);
		jump_start();
		set_col(ctab->w, 0);
		jump_done();
	}
}

void
sel_word(const union arg *arg)
{
	const char *beg, *end, *t;
	struct marker fake;
	struct line *l;

	l = ctab->w->p.l;
	beg = l->s.s + ctab->w->p.col;

	if (arg->i > 0)
		sel_word_nex(&beg, &end);
	else
		sel_word_prv(&beg, &end);

	get_rowcol(&fake);

	fake.col = beg - l->s.s;
	set_rowcol(&fake);
	jump_start();

	fake.col = end - l->s.s;
	set_rowcol(&fake);
	jump_done();

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
yank(const union arg *arg)
{
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
		for (int i = 0; i < fbs.n; i++) {
			if (strcmp(fbs.e[i].path, argv[1]) == 0) {
				fb = &fbs.e[i];
				goto setwin;
			}
		}
	}

	darr_expand(&fbs);
	fb = &fbs.e[fbs.n - 1];
	memset(fb, 0, sizeof(*fb));

	list_init(&fb->lines);

	if (argc <= 1) {
		strcpy(fb->path, "<unnamed>");
	} else {
		strcpy(fb->path, argv[1]);
	}

	if (argc <= 1 || !(fp = fopen(argv[1], "r"))) {
		empty_fbuf(fb);
		goto setwin;
	}

	for (nline = 0; fgets(sbuf, BUFSIZ, fp); nline++) {
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->s, sbuf);
		list_insert(&fb->lines, fb->lines.end, &l->link);
	}
	fb->nline = nline;

	fclose(fp);
setwin:
	fb->pos.fb = fb;
	set_rowcol(&fb->pos);
}

void
cmd_write(int argc, const char *argv[])
{
	FILE *fp;

	if (argc <= 1 || !argv[1]) {
		if (!(fp = fopen(ctab->w->p.fb->path, "w")))
			return;
	} else {
		fp = fopen(argv[1], "w");
	}

	list_for_each(struct line, l, ctab->w->p.fb->lines.beg, tmp, link)
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
	while (running)
		keypress(request_key());
	fini();

	return 0;
}

#define SKB_IMPL
#include "skb.h"
