#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include <grapheme.h>

#include "darr.h"
#include "getarg.h"
#define UTILSH_LIST_STRIP
#include "list.h"
#include "sctui.h"
#include "str.h"
#include "skb.h"
#include "utils.h"

typedef union arg Arg;

#define UCHR_RENDER_MAX 256

#define scrw global_sctui.w
#define scrh global_sctui.h
#define ARG(...) (const Arg){__VA_ARGS__}
#define ARGCV(...) sizeof(ARGV(__VA_ARGS__)) / sizeof(ARGV(__VA_ARGS__)[0]), ARGV(__VA_ARGS__)
#define ARGV(...) (const char *[]){__VA_ARGS__}

#define grapheme_decode_iter(STR, RET, OFF, CP) \
	for (OFF = 0; \
			(RET = grapheme_decode_utf8((STR) + OFF, SIZE_MAX, &(CP))) > 0 \
				&& (CP) != 0; \
			OFF += RET)

#define lineof(LINK) utilsh_list_container_of(LINK, Line, link)

enum { ModeN, ModeI, ModeV };

typedef struct Buf {
	struct utilsh_list_head lines, undos;
	unsigned int nline;
	char path[FILENAME_MAX];
} Buf;

typedef struct Cmd {
	const char *name;
	int (*func)(int argc, const char *argv[]);
} Cmd;

typedef struct Pos {
	unsigned int row, col;
} Pos;
typedef struct Edit {
	Pos beg, end;
	struct str replace;
} Edit;

typedef struct Line {
	struct utilsh_list link;
	struct str s;
} Line;

typedef struct LineIter {
	Pos beg, end;
	Line *l, *nex;
	unsigned int lstart, lend, lrow;
} LineIter;

typedef struct Mark {
	Buf *b;
	Pos p;
	unsigned int rowoff, coloff;
} Mark;

typedef struct Win {
	Buf *b;
	Line *l;
	unsigned int row, rowoff,
	             col, coloff,
	             h;
} Win;
typedef struct Tab {
	Win main, bottom;
	Win *w;
} Tab;

typedef struct Uchr {
	int w, blen;
	uint_least32_t cp;
} Uchr;

typedef struct Undo {
	Edit e;
	struct utilsh_list link;
} Undo;

typedef darr(struct pollfd) pfds_t;
typedef darr(Tab) tabs_t;

static void backspace(const Arg *arg);
static int caninsert(void);
static void cmd(const Arg *arg);
static int cmdedit(int argc, const char *argv[]);
static int cmdquit(int argc, const char *argv[]);
static unsigned int coltobcol(Line *l, unsigned int col);
static void draw(void);
static void drawbar(void);
static void drawline(struct str *s, int selbeg, int selend);
static void drawwin(Win *w, unsigned int y, unsigned int h);
static Edit *edit(const Edit *e);
static void einsert(const struct str *content);
static Line *enewline(Buf *b, Line *at);
static void eremove(unsigned int beg, unsigned int end);
static void eremovem(Pos *beg, Pos *end);
static Line *freadline(FILE *fp);
static Line *getln(Line *curl, unsigned int crow, unsigned int row);
static Mark *getmark(int idx);
static unsigned int getrx(Line *l, unsigned int col);
static void gotomark(const Arg *arg);
static void handlekey(void);
static void insert(const Arg *arg);
static Line *iterln(LineIter *li);
static const char *iterstr(struct str *result, const char *str);
static void makelniter(LineIter *li, Pos *beg, Pos *end);
static void mark(const Arg *arg);
static const Cmd *matchcmd(const char *name);
static void mode(const Arg *arg);
static void movedown(const Arg *arg);
static void moveright(const Arg *arg);
static void newtab(const Arg *arg);
static void pollev(void);
static int pollkey(void);
static void removeln(Buf *b, Line *l);
static size_t renderchr(Uchr *uc, const char *s);
static void setcol(unsigned int col);
static void setrow(unsigned int row);
static void swappos(Pos **beg, Pos **end);

static int cmode = ModeN;
static Tab *ctab;
#define cwin (ctab->w)
static int running;
static int selected;
static pfds_t pfds;
static char sbuf[BUFSIZ], rbuf[UCHR_RENDER_MAX];
static struct str barbuf;
static tabs_t tabs;

/* events */
static int keyev;

/* numbers + lowers + '\'' + '"'
 * '\'': explicit selection by user
 * '"':  implicit selection like some jumping actions */
static Mark marks[10 + 26 + 2];
#define SELMARK marks[10 + 26]

static struct option opts[] = {
	OPT_END
};

#include "config.h"

void
backspace(const Arg *arg)
{
	Edit e = {0};
	e.end.row = cwin->row;
	e.end.col = cwin->col;
	e.beg = e.end;

	if (e.beg.col == 0) {
		if (e.beg.row == 0)
			return;
	} else {
		e.beg.col--;
	}

	edit(&e);
}

int
caninsert(void)
{
	switch (cmode) {
	case ModeI:
		return 1;
	default:
		return 0;
	}
}

void
cmd(const Arg *arg)
{
	darr(char *) args;
	const Cmd *c;
	char *dup = strdup(arg->s), *saver, *tok;

	darr_init(&args);

	for (tok = dup; strtok_r(tok, " \r\n", &saver); tok = NULL)
		darr_append(&args, tok);
	darr_append(&args, NULL);
	args.n--;

	if (!(c = matchcmd(args.e[0])))
		goto clean;

	c->func(args.n, (const char **)args.e);
clean:
	free(args.e);
	free(dup);
}

int
cmdedit(int argc, const char *argv[])
{
	Buf *b;
	FILE *fp;
	Line *l;

	if (!(fp = fopen(argv[1], "r")))
		return 1;

	b = ecalloc(1, sizeof(*b));
	strcpy(b->path, argv[1]);
	list_init(&b->lines);
	for (; (l = freadline(fp)); b->nline++)
		list_insert(&b->lines, b->lines.end, &l->link);
	ctab->main.b = b;
	ctab->main.l = lineof(b->lines.beg);

	return 0;
}

int
cmdquit(int argc, const char *argv[])
{
	running = 0;
	return 0;
}

unsigned int
coltobcol(Line *l, unsigned int col)
{
	unsigned int bcol = 0;
	for (; col; col--)
		bcol += grapheme_next_character_break_utf8(l->s.s + bcol, l->s.len - bcol);
	return bcol;
}

void
draw(void)
{
	drawbar();
	drawwin(&ctab->main, 0, ctab->main.h);
	if (ctab->bottom.h)
		drawwin(&ctab->bottom, ctab->main.h, ctab->bottom.h);
	sctui_move(getrx(cwin->l, cwin->col),
			cwin->row - cwin->rowoff);
	sctui_commit();
}

void
drawbar(void)
{
	estr_append_cstr(&barbuf, modestr[cmode]);
	estr_append_chr(&barbuf, ' ');
	estr_append_cstr(&barbuf, cwin->b->path);
	estr_append_chr(&barbuf, ' ');
	snprintf(sbuf, BUFSIZ, "%u,%u", cwin->row + 1, cwin->col + 1);
	estr_append_cstr(&barbuf, sbuf);
	estr_append_chr(&barbuf, '\n');

	sctui_out(sctui_attr_on(barattr), 0);
	sctui_move(0, scrh);
	drawline(&barbuf, -1, -1);
	sctui_out(sctui_attr_off(), 0);

	estr_clean(&barbuf);
}

void
drawline(struct str *s, int selbeg, int selend)
{
	int i, wsum = 0;
	size_t ret, off;
	Uchr uc;

	for (i = off = 0; (ret = renderchr(&uc, s->s + off)) > 0; off += ret, i++) {
		if (i == selbeg && selend != 0)
			sctui_out(sctui_attr_on(selattr), 0);
		else if (i == selend)
			sctui_out(sctui_attr_off(), 0);
		wsum += uc.w;
		if (wsum > scrw)
			return;
		sctui_out(rbuf, uc.blen);
	}

	for (int c = scrw - wsum; c; c--, i++) {
		if (i == selend)
			sctui_out(sctui_attr_off(), 0);
		sctui_outc(' ');
	}
}

void
drawwin(Win *w, unsigned int y, unsigned int h)
{
	/* shits, don't read it */
	Line *d = getln(w->l, w->row, w->rowoff);
	LineIter iter;
	unsigned int nl;
	int s = selected && w->b == SELMARK.b, selbeg, selend;
	Pos _beg = SELMARK.p, _end, *beg = &_beg, *end = &_end;

	if (s) {
		_end.row = w->row;
		_end.col = w->col;
		swappos(&beg, &end);
		iter.beg = *beg;
		iter.end = *end;
		iter.lrow = w->rowoff;
	}

	for (nl = 0; nl < h; nl++, d = lineof(d->link.nex), iter.lrow++) {
		sctui_move(0, y + nl);
		iter.l = iter.nex = d;
		if (s && iterln(&iter)) {
			selbeg = iter.lstart;
			selend = iter.lend;
		} else {
			selbeg = selend = -1;
		}
		drawline(&d->s, selbeg, selend);
		if (!d->link.nex)
			break;
	}
}

Edit *
edit(const Edit *e)
{
	Pos _beg = e->beg, _end = e->end, *beg = &_beg, *end = &_end;
	unsigned int orow = cwin->row;

	swappos(&beg, &end);

	cwin->col = e->beg.col;
	cwin->row = e->beg.row;
	cwin->l = getln(cwin->l, orow, cwin->row);

	if (e->beg.row == e->end.row)
		eremove(beg->col, end->col);
	else
		eremovem(beg, end);

	if (e->replace.s)
		einsert(&e->replace);

	return NULL;
}

void
einsert(const struct str *content)
{
	unsigned int bcol = coltobcol(cwin->l, cwin->col);
	const char *s = content->s;
	struct str tmp, save = {0};

	estr_from_cstr(&save, cwin->l->s.s + bcol);
	estr_remove(&cwin->l->s, bcol, cwin->l->s.len - bcol);

	while ((s = iterstr(&tmp, s))) {
		if (tmp.len == 1 && tmp.s[0] == '\n') {
			cwin->row++;
			cwin->col = 0;
			cwin->l = enewline(cwin->b, cwin->l);
		} else {
			estr_append_str(&cwin->l->s, &tmp);
			cwin->col += tmp.len;
		}
	}

	if (save.s)
		estr_append_str(&cwin->l->s, &save);

	str_free(&save);
}

/* l1 -> l2
 * l1 -> l3 -> l2 (insert l3 at l1) */
Line *
enewline(Buf *b, Line *at)
{
	Line *l = ecalloc(1, sizeof(*l));
	list_insert(&b->lines, &at->link, &l->link);
	b->nline++;
	return l;
}

void
eremove(unsigned int beg, unsigned int end)
{
	unsigned int bbeg = coltobcol(cwin->l, beg),
	             bend = coltobcol(cwin->l, end);
	estr_remove(&cwin->l->s, bbeg, bend - bbeg);
}

void
eremovem(Pos *beg, Pos *end)
{
	unsigned int bcol = coltobcol(cwin->l, cwin->col);
	Line *begln = cwin->l, *l, *nex;

	estr_remove(&cwin->l->s, bcol, cwin->l->s.len - bcol);

	nex = cwin->l;
	for (unsigned int lrow = beg->row; lrow != end->row; lrow++) {
		l = nex;
		nex = lineof(l->link.nex);
		removeln(cwin->b, l);
	}

	/* l == e->end.row */
	bcol = coltobcol(l, end->col);
	if (bcol < l->s.len)
		estr_append_cstr(&begln->s, l->s.s + bcol);
	removeln(cwin->b, l);
}

Line *
freadline(FILE *fp)
{
	Line *l = ecalloc(1, sizeof(*l));
	struct str s;

	while (fgets(sbuf, BUFSIZ, fp)) {
		estr_from_cstr(&s, sbuf);
		if (l->s.s) {
			estr_append_str(&l->s, &s);
			str_free(&s);
		} else {
			l->s = s;
		}
		if (l->s.s[l->s.len - 1] == '\n')
			break;
	}
	if (!l->s.s) {
		free(l);
		return NULL;
	}
	if (l->s.s[l->s.len - 1] == '\n')
		estr_remove(&l->s, l->s.len - 1, 1);
	return l;
}

Line *
getln(Line *curl, unsigned int crow, unsigned int row)
{
	struct utilsh_list *link = &curl->link;
	for (unsigned int r = crow; r < row; r++)
		link = link->nex;
	for (unsigned int r = crow; r > row; r--)
		link = link->prv;
	return lineof(link);
}

Mark *
getmark(int idx)
{
	if (isdigit(idx)) {
		idx -= '0';
	} else if (islower(idx)) {
		idx = idx - 'a' + 10;
	} else if (idx == '\'') {
		idx = &SELMARK - marks;
	} else {
		return NULL;
	}
	return &marks[idx];

}

unsigned int
getrx(Line *l, unsigned int col)
{
	unsigned int i, rx;
	size_t ret, off;
	Uchr uc;

	i = rx = 0;

	for (off = 0; (ret = renderchr(&uc, l->s.s + off)) > 0; off += ret) {
		if (i >= col)
			break;
		rx += uc.w;
		i++;
	}

	return rx;
}

void
gotomark(const Arg *arg)
{
	unsigned int orow = cwin->row;
	int k = arg->i;
	Mark *m;
	if (k == 0)
		k = pollkey();
	if (!(m = getmark(k)))
		return;
	if (!m->b)
		return;
	cwin->b = m->b;
	cwin->row = m->p.row;
	cwin->col = m->p.col;
	cwin->rowoff = m->rowoff;
	cwin->coloff = m->coloff;
	cwin->l = getln(cwin->l, orow, cwin->row);
}

void
handlekey(void)
{
	char *buf = sbuf;
	int ret = skb_keypress(keyev, keys[cmode]);

	keyev = 0;
	switch (ret) {
	case -1:
		if (!caninsert())
			goto drop;
		for (int i = 0; i < skb_ncombo; i++, buf++) {
			if (iscntrl(skb_combo[i]) && skb_combo[i] != '\n')
				goto drop;
			*buf = skb_combo[i];
		}
		for (int l = utf8_blen(buf[-1]) - 1; l > 0; l--, buf++)
			*buf = sctui_grab_key();
		*buf = 0;
		skb_dropcombo();
		insert(&ARG(.s = sbuf));
		break;
	case 0:
		return;
	case 1:
		break;
	}
drop:
	skb_dropcombo();
}

void
insert(const Arg *arg)
{
	Edit e = {0};
	e.beg.row = cwin->row;
	e.beg.col = cwin->col;
	e.end = e.beg;
	estr_from_cstr(&e.replace, arg->s);

	edit(&e);

	str_free(&e.replace);
}

Line *
iterln(LineIter *li)
{
	if (li->nex == NULL)
		return NULL;

	/* not the begging */
	if (li->l != li->nex)
		li->lrow++;

	li->l = li->nex;
	if (li->lrow < li->beg.row || li->lrow > li->end.row) {
		return NULL;
	} else if (li->beg.row == li->end.row) {
		if (li->beg.col == li->end.col)
			return NULL;
		li->lstart = li->beg.col;
		li->lend = li->end.col;
		goto end;
	} else if (li->lrow == li->beg.row) {
		li->lstart = li->beg.col;
		li->lend = ustrlen(li->l->s.s) + 1;
	} else if (li->lrow == li->end.row) {
		li->lstart = 0;
		li->lend = li->end.col;
		goto end;
	} else {
		li->lstart = 0;
		li->lend = ustrlen(li->l->s.s) + 1;
	}

	li->nex = lineof(li->l->link.nex);
	return li->l;
end:
	li->nex = NULL;
	return li->l;
}

const char *
iterstr(struct str *result, const char *str)
{
	result->s = (char*)str;
	for (; *str && *str != '\n'; str++);
	result->len = result->siz = str - result->s;
	if (result->len == 0) {
		if (*str != '\n')
			return NULL;
		str++;
		result->len = result->siz = 1;
	}
	return str;
}

void
makelniter(LineIter *li, Pos *beg, Pos *end)
{
	swappos(&beg, &end);

	li->beg = *beg;
	li->end = *end;

	//li->l = li->nex = getln(refln, refrow, li->beg->row);
	li->lrow = li->beg.row;
}

void
mark(const Arg *arg)
{
	int k = arg->i;
	Mark *m;
	if (k == 0)
		k = pollkey();
	if (!(m = getmark(k)))
		return;
	m->b = cwin->b;
	m->p.row = cwin->row;
	m->p.col = cwin->col;
	m->rowoff = cwin->rowoff;
	m->coloff = cwin->coloff;
}

const Cmd *
matchcmd(const char *name)
{
	for (const Cmd *c = cmds; c->name; c++) {
		if (strcmp(c->name, name) == 0)
			return c;
	}
	return NULL;
}

void
mode(const Arg *arg)
{
	int omode = cmode;
	cmode = arg->i;
	switch (cmode) {
	case ModeV:
		mark(&ARG(.i = '\''));
		selected = 1;
		break;
	default:
		if (omode == ModeV)
			selected = 0;
		break;
	}
}

void
movedown(const Arg *arg)
{
	if (cwin->row == 0 && arg->i < 0)
		return;
	setrow(cwin->row + arg->i);
}

void
moveright(const Arg *arg)
{
	if (cwin->col == 0 && arg->i < 0)
		return;
	setcol(cwin->col + arg->i);
}

void
newtab(const Arg *arg)
{
	darr_expand(&tabs);
	ctab = &darr_last(&tabs);
	memset(ctab, 0, sizeof(*ctab));
	cwin = &ctab->main;
	cwin->h = scrh - 1;

	if (!arg || !arg->s)
		return;

	cmdedit(ARGCV("e", arg->s));
}

void
pollev(void)
{
	keyev = 0;
	if (poll(pfds.e, pfds.n, -1) == -1 && errno != EINTR)
		die("poll()");
	if (pfds.e[0].revents & POLLIN)
		keyev = sctui_grab_key();
}

int
pollkey(void)
{
	keyev = 0;
	while (!keyev)
		pollev();
	return keyev;
}

void
removeln(Buf *b, Line *l)
{
	list_remove(&b->lines, &l->link);
	str_free(&l->s);
	free(l);
}

size_t
renderchr(Uchr *uc, const char *s)
{
	size_t ret = grapheme_decode_utf8(s, SIZE_MAX, &uc->cp);
	char *sb = rbuf;

	if (ret == 0 || uc->cp == 0)
		return 0;

	uc->blen = ret;
	if (ret == 1)
		sb[0] = *s;
	else
		strncpy(sb, s, ret);
	switch (uc->cp) {
	case L'\n':
		return 0;
	case L'\t':
		uc->blen = uc->w = strlen(tabrender);
		strcpy(sb, tabrender);
		break;
	default:
		if ((uc->w = wcwidth(uc->cp)) <= 0) {
			uc->w = 2;
			sb += sprintf(sb, "%s", sctui_attr_on(nonprintattr));
			toprint(sb, uc->cp);
			sb += 2;
			sb += sprintf(sb, "%s", sctui_attr_off());
			sb += sprintf(sb, "%s", sctui_attr_last());
			uc->blen = sb - rbuf;
		}
		break;
	}

	return ret;
}

void
setcol(unsigned int col)
{
	cwin->col = align(col, 0, ustrlen(cwin->l->s.s));
	if (cwin->col < cwin->coloff)
		cwin->coloff = cwin->col;
	else if (cwin->col >= cwin->coloff + scrw)
		cwin->coloff = cwin->col - scrw + 1;
}

void
setrow(unsigned int row)
{
	unsigned int orow = cwin->row;
	cwin->row = align(row, 0, cwin->b->nline - 1);
	if (cwin->row < cwin->rowoff)
		cwin->rowoff = cwin->row;
	else if (cwin->row >= cwin->rowoff + cwin->h)
		cwin->rowoff = cwin->row - cwin->h + 1;
	cwin->l = getln(cwin->l, orow, cwin->row);
	setcol(cwin->col);
}

void
swappos(Pos **beg, Pos **end)
{
	Pos *b = *beg, *e = *end;
	if (b->row > e->row
	|| (b->row == e->row && b->col > e->col)) {
		*beg = e;
		*end = b;
	}
}

int
main(int argc, char *argv[])
{
	char *entry = NULL;
	enum GETARG_RESULT r;
	GETARG_BEGIN(r, argc, argv, opts) {
	case GETARG_RESULT_SUCCESSFUL:
		break;
	case GETARG_RESULT_UNKNOWN:
		if (entry)
			die("too many entry files\n");
		entry = *argv;
		GETARG_SHIFT(argc, argv);
		break;
	default:
		return 1;
	} GETARG_END;

	/* for the fucking wcwidth() */
	setlocale(LC_ALL, "");
	sctui_init();

	darr_init(&pfds);
	darr_expand(&pfds);
	pfds.e[0].fd = STDIN_FILENO;
	pfds.e[0].events = POLLIN;

	darr_init(&tabs);
	newtab(&ARG(.s = entry));

	str_empty(&barbuf);

	sctui_open_alt_screen();

	running = 1;
	while (running) {
		draw();
		pollev();
		handlekey();
	}

	sctui_close_alt_screen();
	sctui_commit();
	sctui_fini();

	return 0;
}
