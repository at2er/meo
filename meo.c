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
#include "darr.h"
#include "getarg.h"
#include "list.h"
#include "sctui.h"

#include "meo.h"
#include "utils.h"

#define MAX_MACHES 1

#define ARG(...) (union arg){__VA_ARGS__}
#define lineof(LINK) list_container_of(LINK, struct line, link)
#define refreshl(LREF) (*(LREF)->r = 0)
#define refreshw(WREF) ((WREF)->refresh = 1)

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
static void get_line_nex(struct win *w, int step);
static void get_line_prv(struct win *w, int step);
static struct marker *get_marker(int k);
static char **get_reg(int k);
static void get_rowcol(struct marker *m);
static int get_rx(int col);
static int get_ry(void);
static int get_sel_area(int *beg, int *end);
static void init(void);
static void jumping(void);
static void keypress(int k);
static int match(const char *str);
static int mode_can_insert(void);
static void render_line(const struct win *w, struct line *l);
static int request_key(void);
static void ruler(void);
static int search_nex(void);
static int search_prv(void);
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
copy(const char *s)
{
	const char **cmd = NULL;
	int fds[2];

	if (!(cmd = copy_cmd()))
		return;

	if (pipe(fds) < 0)
		die("pipe()");

	if (fork() == 0) {
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
	wait(NULL);
}

void
draw(void)
{
	for (int i = 0; i < nwin; i++)
		draw_win(&wins[i]);

	ruler();
	draw_win(&bar);

	draw_sel();
	sctui_move(get_rx(ctab->w->col), get_ry());

	sctui_commit();
}

void
draw_sel(void)
{
	const char *tmp;
	int beg, to;
	char buf[256], *p;

	if (!get_sel_area(&beg, &to))
		return;

	tmp = sctui_attr_on(sel_attr);
	beg = get_rx(beg);
	to = get_rx(to);

	p = buf;
	p += sprintf(p, tmp);
	p += sprintf(p, "%.*s", to - beg, has_sel->r + beg - ctab->w->x);

	sctui_text(beg, get_ry(), buf, p - buf);
	tmp = sctui_attr_off();
	sctui_out(tmp, strlen(tmp));
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
	refreshl(l);
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

void
get_line_nex(struct win *w, int step)
{
	list_for_each(struct line, l, &w->l->link, tmp, link) {
		if (step <= 0) {
			w->l = l;
			break;
		}
		step--;
	}
}

void
get_line_prv(struct win *w, int step)
{
	list_for_each_prv(struct line, l, &w->l->link, tmp, link) {
		if (step <= 0) {
			w->l = l;
			break;
		}
		step--;
	}
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
get_rx(int col)
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
get_ry(void)
{
	return ctab->w->y + ctab->w->row - ctab->w->rowoff;
}

int
get_sel_area(int *beg, int *end)
{
	int b = SEL_MARKER.col, e = ctab->w->col;
	if (!has_sel) {
		*beg = *end = 0;
		return 0;
	}
	if (b > e)
		xor_swap(b, e);
	*beg = b;
	*end = e;
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
		refreshl(prv);
	refreshl(l);
	refreshw(ctab->w);
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

	refreshl(l);
	refreshw(&bar);
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
	}

	if (w->row > orig)
		get_line_nex(w, w->row - orig);
	else if (w->row < orig)
		get_line_prv(w, orig - w->row);
	get_draw_line(w);

	set_col(w, w->col);
}

void
set_rowcol(struct marker *m)
{
	ctab->w->fb = m->fb;
	ctab->w->l = m->l;
	ctab->w->rowoff = m->rowoff;
	set_row(ctab->w, m->row);
	set_col(ctab->w, m->col);
}

/* key functions */
void
concat_line(const union arg *arg)
{
	struct line *l = ctab->w->l, *prv;

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

	refreshl(prv);
	refreshw(ctab->w);
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
		refreshl(bar.l);
		refreshw(&bar);
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
		if (pattern)
			regfree(pattern);
		if (!pattern)
			pattern = ecalloc(1, sizeof(*pattern));
		if (regcomp(pattern, dup, REG_NEWLINE)) {
			free(pattern);
			pattern = NULL;
		}
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
	struct line *l = ctab->w->l;
	int pos, len, t;

	if (arg->i == 0) {
		if (has_sel) {
			get_sel_area(&pos, &t);
			len = t - pos;
		} else {
			pos = ctab->w->col;
			len = 1;
			if (pos >= (int)ctab->w->l->s.len - 1) {
				concat_line(&ARG(0));
				return;
			}
		}
	} else {
		pos = ctab->w->col - arg->i;
		len = arg->i;
	}

	if (pos < 0) {
		concat_line(&ARG(.i = -1));
		return;
	}

	if (arg->i == 0 && has_sel)
		dup_to_reg('+', l->s.s + pos, len);
	estr_remove(&l->s, pos, len);
	refreshl(l);
	refreshw(ctab->w);
	move_col(&ARG(.i = arg->i == 0 ? 0 : -len));
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

	refreshl(l);
	refreshw(ctab->w);

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
sel_word(const union arg *arg)
{
	const char *beg, *end;
	struct marker fake;
	struct line *l;

	l = ctab->w->l;
	beg = l->s.s + ctab->w->col;

	while (isspace(*beg))
		beg++;
	end = beg;

	while (isalpha(*end) || *end == '_')
		end++;
	do {
		if (beg == l->s.s)
			break;
		beg--;
		if (!isalpha(*beg) && *beg != '_') {
			beg++;
			break;
		}
	} while (1);

	get_rowcol(&fake);

	fake.col = beg - l->s.s;
	set_rowcol(&fake);
	mark(&ARG(.i = '\''));

	fake.col = end - l->s.s;
	set_rowcol(&fake);

	has_sel = l;
	refreshw(ctab->w);
}

void
yank(const union arg *arg)
{
	struct line *l = ctab->w->l;
	int beg, end;

	if (!get_sel_area(&beg, &end))
		return;

	dup_to_reg(arg->i, l->s.s + beg, end - beg);
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
		refreshl(l);
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
