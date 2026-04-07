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
#include "darr.h"
#include "getarg.h"
#include "list.h"
#include "sctui.h"

#include "meo.h"
#include "utils.h"

#define MAX_MACHES 1

/* inside the selection, other line must handle by hand */
#define for_each_sel(LINE, ROW, NEX, TMP) \
	do { \
		(LINE) = ctab->w->row > SEL_MARKER.row \
				? SEL_MARKER.l : ctab->w->l; \
		(LINE) = lineof((LINE)->link.nex); \
		(NEX) = lineof((LINE)->link.nex); \
		for (int ROW = MIN(ctab->w->row, SEL_MARKER.row) + 1, \
				TMP = MAX(ctab->w->row, SEL_MARKER.row); \
				(LINE) && ROW < TMP; \
				ROW++, (LINE) = (NEX), \
				(NEX) = lineof((LINE)->link.nex))
#define for_each_sel_end \
	} while (0);

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
static int get_sel_area(int *beg, int *end, int *beg2, int *end2);
static void init(void);
static void jumping(void);
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

static struct fbuf *fbs;
static struct tab *tabs;
static struct win *wins;
static int         nfb;
static int         ntab;
static int         nwin;

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
	for (int i = 0; i < nwin; i++)
		draw_win(&wins[i]);

	ruler();
	draw_win(&bar);

	draw_sel();
	sctui_move(get_rx(ctab->w->l, ctab->w->col),
			get_ry(ctab->w->row));

	sctui_commit();
}

void
draw_sel(void)
{
	int beg, end, beg2, end2;
	int rx, ry, len;
	struct line *l, *nex;

	if (!get_sel_area(&beg, &end, &beg2, &end2))
		return;

	sctui_out(sctui_attr_on(sel_attr), 0);

	rx = get_rx(ctab->w->l, beg);
	len = get_rx(ctab->w->l, end) - rx;
	sctui_text(rx, get_ry(ctab->w->row), ctab->w->l->r + rx,
			MIN(len, ctab->w->w));

	rx = get_rx(SEL_MARKER.l, beg2);
	len = get_rx(SEL_MARKER.l, end2) - rx;
	sctui_text(rx, get_ry(SEL_MARKER.row), SEL_MARKER.l->r + rx,
			MIN(len, ctab->w->w));

	ry = get_ry(MIN(ctab->w->row, SEL_MARKER.row) + 1);
	for_each_sel(l, row, nex, tmp) {
		len = get_rx(l, l->s.len);
		sctui_text(0, ry, l->r, MIN(len, ctab->w->w));
		ry++;
	} for_each_sel_end;

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
	int i = 0, c = w->row - w->rowoff;
	if (w->row == w->rowoff) {
		w->draw = w->l;
		return;
	}

	list_for_each_prv(struct line, l, &w->l->link, tmp, link) {
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
	m->fb = ctab->w->fb;
	m->l = ctab->w->l;
	m->row = ctab->w->row;
	m->rowoff = ctab->w->rowoff;
	m->col = ctab->w->col;
}

int
get_rx(struct line *l, int col)
{
	int rx = 0;
	for (int i = 0; i < col; i++) {
		switch (ctab->w->l->s.s[i]) {
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
	return ctab->w->y + row - ctab->w->rowoff;
}

int
get_sel_area(int *beg, int *end, int *beg2, int *end2)
{
	if (!has_sel) {
		*beg = *end = 0;
		return 0;
	}

	if (SEL_MARKER.row > ctab->w->row) {
		*beg = ctab->w->col;
		*end = ctab->w->l->s.len;
		*beg2 = 0;
		*end2 = SEL_MARKER.col + 1;
	} else if (SEL_MARKER.row < ctab->w->row) {
		*beg = 0;
		*end = ctab->w->col;
		*beg2 = SEL_MARKER.col;
		*end2 = SEL_MARKER.l->s.len;
	} else {
		*beg = SEL_MARKER.col;
		*end = ctab->w->col;
		if (*beg > *end)
			xor_swap(*beg, *end);
		*beg2 = *beg;
		*end2 = *end;
		if (*beg == *end)
			return 0;
	}

	return 1;
}

void
init(void)
{
	struct line *l;
	sctui_init();
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;

	nwin = 1;
	wins = erealloc(wins, sizeof(*wins) * nwin);

	ntab = 1;
	tabs = erealloc(tabs, sizeof(*tabs) * ntab);
	ctab = tabs;
	ctab->w = wins;
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
	bar.fb = &rulerbuf;
	bar.fb->nline = 1;
	bar.l = bar.draw = lineof(bar.fb->lines.beg);
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
	has_sel = ctab->w->l;
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
		matched = has_sel = ctab->w->l;
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
	struct line *l, *prv = ctab->w->l;
	struct str s;

	ctab->w->fb->ldirty = 1;

	l = ecalloc(1, sizeof(*l));
	if (!(arg->i & 1))
		prv = lineof(prv->link.prv);
	list_insert(&ctab->w->fb->lines,
			prv ? &prv->link : NULL,
			&l->link);
	ctab->w->fb->nline++;

	if (arg->i & 1 << 1 && prv) {
		s.s = prv->s.s + ctab->w->col;
		s.len = prv->s.len - ctab->w->col;
		s.siz = s.len + 1;
		if (s.len > 0)
			estr_append_str(&l->s, &s);
		estr_remove(&prv->s, ctab->w->col, s.len - 1);
	} else {
		estr_from_cstr(&l->s, "\n");
	}

	set_col(ctab->w, 0);
	move_row(&ARG(.i = (arg->i & 1)));

	if (prv)
		refreshl(ctab->w, prv);
	refreshl(ctab->w, l);
}

void
paste(const union arg *arg)
{
	char **reg = get_reg(arg->i);
	if (!reg || !*reg)
		return;
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

	len = snprintf(sbuf, BUFSIZ, "%d,%d", ctab->w->row, ctab->w->col);

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
		if (ctab->w->row >= ctab->w->fb->nline - 1)
			return 0;
		move_row(&ARG(.i = 1));
	}
	do {
		if (match(ctab->w->l->s.s))
			return 1;
		if (ctab->w->row >= ctab->w->fb->nline - 1)
			break;
		move_row(&ARG(.i = 1));
	} while (1);

	return 0;
}

int
search_prv(void)
{
	if (matched) {
		if (ctab->w->row <= 0)
			return 0;
		move_row(&ARG(.i = -1));
	}

	do {
		if (match(ctab->w->l->s.s))
			return 1;
		if (ctab->w->row <= 0)
			break;
		move_row(&ARG(.i = 1));
	} while (1);

	return 0;
}

void
sel_word_nex(const char **beg, const char **end)
{
	const char *b = *beg, *e = *end;
	while (!isalpha(*b))
		b++;
	e = b;

	while (isalpha(*e) || *e == '_')
		e++;
	do {
		if (b == ctab->w->l->s.s)
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
		if (e == ctab->w->l->s.s)
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
		if (b == ctab->w->l->s.s)
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
	bar.fb = fb;
	bar.l = bar.draw = lineof(fb->lines.beg);
	set_col(&bar, 0);
	refreshw(&bar);
}

void
set_col(struct win *w, int col)
{
	ctab->w->col = align(col, 0, ctab->w->l->s.len - 1);
	ctab->w->fb->pos.col = ctab->w->col;
	refreshw(ctab->w);
}

void
set_row(struct win *w, int row)
{
	int orig = w->row;

	w->row = align(row, 0, w->fb->nline - 1);
	if (w->row <= w->rowoff)
		w->rowoff = w->row;
	else if (w->row >= w->rowoff + w->h)
		w->rowoff = w->row - w->h + 1;
	w->fb->pos.row = w->row;
	w->fb->pos.rowoff = w->rowoff;

	refreshw(w);

	if (w->row == 0) {
		w->l = lineof(w->fb->lines.beg);
	} else if (w->row == w->fb->nline - 1) {
		w->l = lineof(w->fb->lines.end);
	} else if (w->row > orig) {
		w->l = get_line_nex(w->l, w->row - orig);
	} else if (w->row < orig) {
		w->l = get_line_prv(w->l, orig - w->row);
	}
	get_draw_line(w);

	set_col(w, w->col);
}

void
set_rowcol(struct marker *m)
{
	ctab->w->fb = m->fb;
	if (ctab->w->fb->ldirty)
		m->l = get_line_nex(lineof(m->fb->lines.beg), m->row);
	ctab->w->l = m->l;
	ctab->w->row = m->row;
	ctab->w->rowoff = m->rowoff;
	set_row(ctab->w, m->row);
	set_col(ctab->w, m->col);
}

/* key functions */
void
concat_line(const union arg *arg)
{
	struct line *l = ctab->w->l, *prv;

	ctab->w->fb->ldirty = 1;

	if (arg->i == -1) {
		prv = lineof(l->link.prv);
		if (!prv)
			return;
		ctab->w->col = prv->s.len;
		move_row(&ARG(.i = -1));
	} else {
		prv = l;
		l = lineof(l->link.nex);
		if (!l)
			return;
	}

	prv->s.len -= 1; /* remove the '\n' */
	estr_append_str(&prv->s, &l->s);

	list_remove(&ctab->w->fb->lines, &l->link);

	str_free(&l->s);
	free(l);

	refreshl(ctab->w, prv);
}

void
backspace(const union arg *arg)
{
	struct line *l = ctab->w->l;
	int pos = ctab->w->col - arg->i;
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
	int argc = 0, m = cmode;
	char **argv = NULL;
	char *tok, *dup, *saver;

	if (arg->s) {
		dup = strdup(arg->s);
	} else {
		dup = strdup(bar.l->s.s);
		if (dup[bar.l->s.len - 1] == '\n')
			dup[bar.l->s.len - 1] = '\0';
		bar.l->s.s[0] = '\n';
		bar.l->s.s[1] = '\0';
		bar.l->s.len = 1;
		refreshl(&bar, bar.l);
	}

	mode(&ARG(.i = MODE_NOR));

	switch (m) {
	case MODE_CMD:
		for (tok = dup; ; tok = NULL) {
			if (!(tok = strtok_r(tok, " \t\n", &saver)))
				break;
			darr_append(argv, argc, tok);
		}
		darr_append(argv, argc, NULL);
		argc--;
		break;
	case MODE_SEARCH:
		comp_pattern(dup, 0);
		match(ctab->w->l->s.s);
		break;
	}

	if (!argc || !argv[0])
		goto end;

	for (int i = 0; cmds[i].cmd != NULL; i++) {
		if (strcmp(cmds[i].cmd, argv[0]) == 0 ||
		    strcmp(cmds[i].alias, argv[0]) == 0) {
			cmds[i].func(argc, (const char**)argv);
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
	struct line *l = ctab->w->l, *nex;
	int beg, end, beg2, end2;
	int to_row, to_col;

	if (!get_sel_area(&beg, &end, &beg2, &end2)) {
		if (ctab->w->col >= (int)ctab->w->l->s.len - 1)
			concat_line(&ARG(0));
		else
			estr_remove(&l->s, ctab->w->col, 1);
		refreshl(ctab->w, l);
		return;
	}

	str_empty(&buf);

	for_each_sel(l, row, nex, tmp) {
		remove_line(ctab->w->fb, l);
	} for_each_sel_end;

	l = ctab->w->l;
	estr_remove(&l->s, beg, end - beg);
	refreshl(ctab->w, ctab->w->l);

	if (SEL_MARKER.row != ctab->w->row) {
		estr_remove(&SEL_MARKER.l->s, beg2, end2 - beg2);
		refreshl(ctab->w, SEL_MARKER.l);
	}

	to_row = ctab->w->row;
	to_col = beg;
	if (SEL_MARKER.row < ctab->w->row) {
		estr_append_str(&SEL_MARKER.l->s, &l->s);
		remove_line(ctab->w->fb, l);
		to_row = SEL_MARKER.row;
		to_col = beg2;
	} else if (SEL_MARKER.row > ctab->w->row) {
		estr_append_str(&l->s, &SEL_MARKER.l->s);
		remove_line(ctab->w->fb, SEL_MARKER.l);
	}

	set_row(ctab->w, to_row);
	set_col(ctab->w, to_col);

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
		set_row(ctab->w, ctab->w->fb->nline - 1);
		break;
	case GOTO_IN_LINE:
		jumping();
		set_col(ctab->w, ctab->w->l->s.len - 1);
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
	struct line *l = ctab->w->l;
	struct str s;

	s.s = (char*)arg->s;
	s.len = 0;
	for (c = arg->s; *c; c++) {
		if (*c == '\n') {
			estr_insert_str(&l->s, ctab->w->col - s.len, &s);
			s.s = (char*)(c + 1);
			s.siz = s.len = 0;
			new_line(&ARG(.i = 0x3)); /* 0b11 */
			continue;
		}
		s.len++;
		ctab->w->col++;
	}

	refreshl(ctab->w, l);

	estr_insert_str(&l->s, ctab->w->col - s.len, &s);
	set_col(ctab->w, ctab->w->col);
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
	set_col(ctab->w, ctab->w->col + arg->i);
	has_sel = NULL;
}

void
move_row(const union arg *arg)
{
	set_row(ctab->w, ctab->w->row + arg->i);
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
	has_sel = SEL_MARKER.l;
}

void
sel_line(const union arg *arg)
{
	if (arg->i > 0) {
		set_col(ctab->w, 0);
		jumping();
		set_col(ctab->w, ctab->w->l->s.len - 1);
	} else {
		set_col(ctab->w, ctab->w->l->s.len - 1);
		jumping();
		set_col(ctab->w, 0);
	}
}

void
sel_word(const union arg *arg)
{
	const char *beg, *end, *t;
	struct marker fake;
	struct line *l;

	l = ctab->w->l;
	beg = l->s.s + ctab->w->col;

	if (arg->i > 0)
		sel_word_nex(&beg, &end);
	else
		sel_word_prv(&beg, &end);

	get_rowcol(&fake);

	fake.col = beg - l->s.s;
	set_rowcol(&fake);
	jumping();

	fake.col = end - l->s.s;
	set_rowcol(&fake);

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
	// struct line *l = ctab->w->l;
	// int beg, end;

	// if (!get_sel_area(&beg, &end))
	// 	return;

	// dup_to_reg(arg->i, l->s.s + beg, end - beg);
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
	set_rowcol(&(struct marker){
			fb, lineof(fb->lines.beg),
			fb->pos.row,
			fb->pos.rowoff,
			fb->pos.col});
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
	while (running)
		keypress(request_key());
	fini();

	return 0;
}

#define SKB_IMPL
#include "skb.h"
