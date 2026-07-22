#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h> /* shit? */
#include <wctype.h>

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

#define scrw global_sctui.w
#define scrh global_sctui.h
#define ARG(...) (const Arg){__VA_ARGS__}
#define ARGCV(...) sizeof(ARGV(__VA_ARGS__)) / sizeof(ARGV(__VA_ARGS__)[0]), \
		ARGV(__VA_ARGS__)
#define ARGV(...) (const char *[]){__VA_ARGS__}

#define grapheme_decode_iter(STR, RET, OFF, CP) \
	for (; (RET = grapheme_decode_utf8((STR) + OFF, SIZE_MAX, &(CP))) > 0 \
				&& (CP) != 0; \
			OFF += RET)

#define lineof(LINK) utilsh_list_container_of(LINK, Line, link)

enum { ModeN, ModeC, ModeI, ModeV };

typedef struct Buf {
	struct utilsh_list_head lines, undos;
	unsigned int nline;
	char path[FILENAME_MAX];
} Buf;

typedef struct Cmd {
	const char *name;
	void (*func)(int argc, const char *argv[]);
} Cmd;

typedef struct Pos {
	unsigned int row, col;
} Pos;
typedef struct Edit {
	Pos beg, end, cursor;
	struct str replace;
	unsigned int setcursor:1;
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
	             col, coloff;
	unsigned int x, y, h, w;

	unsigned int orow, ocol;
} Win;
typedef struct Tab {
	Win main, bottom;
	Win *w, *ow;
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

#include "textobj.h"

static void backspace(const Arg *arg);
static int caninsert(void);
static void cmd(const Arg *arg);
static void cmdedit(int argc, const char *argv[]);
static void cmdquit(int argc, const char *argv[]);
static void cmdwrite(int argc, const char *argv[]);
static unsigned int coltobcol(Line *l, unsigned int col);
static void delete(const Arg *arg);
static void draw(void);
static void drawbar(void);
static void drawcmdline(void);
static void drawline(struct str *s, unsigned int x, int selbeg, int selend);
static void drawwin(Win *w);
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
static void initcmdbuf(void);
static void insert(const Arg *arg);
static Line *iterln(LineIter *li);
static const char *iterstr(struct str *result, const char *str);
static void makelniter(LineIter *li, Pos *beg, Pos *end);
static void mark(const Arg *arg);
static const Cmd *matchcmd(const char *name);
static void mode(const Arg *arg);
static void movedown(const Arg *arg);
static void moveright(const Arg *arg);
static void newline(const Arg *arg);
static void newtab(const Arg *arg);
static void pollev(void);
static int pollkey(void);
static void removeln(Buf *b, Line *l);
static size_t renderchr(Uchr *uc, const char *s);
static void selline(const Arg *arg);
static void seltextobj(const TextObj *t);
static void selword(const Arg *arg);
static void setcol(Win *w, unsigned int col);
static void setrow(Win *w, unsigned int row);
static void swappos(Pos **beg, Pos **end);
static void update(void);

static Buf cmdbuf;
static Win cmdline;
static int cmode = ModeN;
static Tab *ctab;
#define cwin (ctab->w)
static int running;
static int selected;
static pfds_t pfds;
static char sbuf[BUFSIZ], rbuf[BUFSIZ];
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
	case ModeC: case ModeI:
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
	char *dup = NULL, *saver, *tok;

	if (arg->s)
		dup = strdup(arg->s);
	if (cmode == ModeC) {
		mode(&ARG(.i = ModeN));
		if (!cmdline.l->s.s[0])
			return;
		if (!dup)
			dup = strdup(cmdline.l->s.s);
	}

	if (!dup)
		return;

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

void
cmdedit(int argc, const char *argv[])
{
	Buf *b;
	FILE *fp;
	Line *l;

	if (!(fp = fopen(argv[1], "r")))
		return;

	b = ecalloc(1, sizeof(*b));
	strcpy(b->path, argv[1]);
	list_init(&b->lines);
	for (; (l = freadline(fp)); b->nline++)
		list_insert(&b->lines, b->lines.end, &l->link);
	ctab->main.b = b;
	ctab->main.l = lineof(b->lines.beg);
}

void
cmdquit(int argc, const char *argv[])
{
	running = 0;
}

void
cmdwrite(int argc, const char *argv[])
{
	FILE *fp;
	const char *path;

	if (argc <= 1 || !argv[1])
		path = cwin->b->path;
	else
		path = argv[1];

	if (!(fp = fopen(path, "w")))
		return;

	list_for_each(Line, l, cwin->b->lines.beg, tmp, link) {
		fputs(l->s.s, fp);
		fputc('\n', fp);
	}

	fclose(fp);
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
delete(const Arg *arg)
{
	Edit e = {0};
	e.beg.col = cwin->col;
	e.beg.row = cwin->row;
	if (cmode == ModeV) {
		e.end.row = SELMARK.p.row;
		e.end.col = SELMARK.p.col;
	} else {
		e.end = e.beg;
		e.end.col++;
	}
	edit(&e);
}

void
draw(void)
{
	if (cmode == ModeC)
		drawcmdline();
	else
		drawbar();
	drawwin(&ctab->main);
	if (ctab->bottom.h)
		drawwin(&ctab->bottom);
	sctui_move(cwin->x + getrx(cwin->l, cwin->col),
			cwin->y + cwin->row - cwin->rowoff);
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
	drawline(&barbuf, scrw, -1, -1);
	sctui_out(sctui_attr_off(), 0);

	estr_clean(&barbuf);
}

void
drawcmdline(void)
{
	drawwin(&cmdline);
}

void
drawline(struct str *s, unsigned int w, int selbeg, int selend)
{
	int i;
	unsigned int wsum = 0;
	size_t ret, off;
	Uchr uc;

	for (i = off = 0; (ret = renderchr(&uc, s->s + off)) > 0; off += ret, i++) {
		if (i == selbeg && selend != 0)
			sctui_out(sctui_attr_on(selattr), 0);
		else if (i == selend)
			sctui_out(sctui_attr_off(), 0);
		wsum += uc.w;
		if (wsum > w)
			return;
		sctui_out(rbuf, uc.blen);
	}

	for (unsigned int c = w - wsum; c; c--, i++) {
		if (i == selend)
			sctui_out(sctui_attr_off(), 0);
		sctui_outc(' ');
	}
}

void
drawwin(Win *w)
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

	for (nl = 0; nl < w->h; nl++, d = lineof(d->link.nex), iter.lrow++) {
		sctui_move(w->x, w->y + nl);
		iter.l = iter.nex = d;
		if (s && iterln(&iter)) {
			selbeg = iter.lstart;
			selend = iter.lend;
		} else {
			selbeg = selend = -1;
		}
		drawline(&d->s, w->w, selbeg, selend);
		if (!d->link.nex)
			break;
	}

	sctui_fill_space(rbuf, 0, w->w);
	for (nl++; nl < w->h; nl++) {
		sctui_move(w->x, w->y + nl);
		sctui_out(rbuf, 0);
	}
}

Edit *
edit(const Edit *e)
{
	Pos _beg = e->beg, _end = e->end, *beg = &_beg, *end = &_end;

	swappos(&beg, &end);

	cwin->l = getln(cwin->l, cwin->row, beg->row);
	cwin->col = beg->col;
	cwin->row = beg->row;

	if (beg->row == end->row)
		eremove(beg->col, end->col);
	else
		eremovem(beg, end);

	if (e->replace.s)
		einsert(&e->replace);

	if (e->setcursor) {
		cwin->l = getln(cwin->l, cwin->row, e->cursor.row);
		cwin->row = e->cursor.row;
		cwin->col = e->cursor.col;
	}

	if (cmode == ModeV)
		mode(&ARG(.i = ModeN));

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
	unsigned int bbeg, bend;
	Pos fbeg, fend; /* fake */
	Line *l = cwin->l;

	if (beg == end)
		return;

	if (end > ustrlen(l->s.s)) {
		fbeg.row = cwin->row;
		fbeg.col = cwin->col;
		fend.row = fbeg.row + 1;
		fend.col = 0;
		eremovem(&fbeg, &fend);
		return;
	}
	bbeg = coltobcol(l, beg);
	bend = coltobcol(l, end);
	estr_remove(&l->s, bbeg, bend - bbeg);
}

void
eremovem(Pos *beg, Pos *end)
{
	unsigned int bcol = coltobcol(cwin->l, cwin->col);
	Line *begln = cwin->l, *l, *nex;

	estr_remove(&begln->s, bcol, begln->s.len - bcol);

	l = nex = lineof(begln->link.nex);
	for (unsigned int lrow = beg->row + 1; lrow != end->row; lrow++) {
		nex = lineof(l->link.nex);
		removeln(cwin->b, l);
		l = nex;
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
			if (iscntrl(skb_combo[i]) && !strchr("\t\n", skb_combo[i]))
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
initcmdbuf(void)
{
	Line *l = ecalloc(1, sizeof(*l));
	estr_from_cstr(&l->s, "");
	list_init(&cmdbuf.lines);
	list_insert(&cmdbuf.lines, cmdbuf.lines.end, &l->link);
	cmdbuf.nline = 1;
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
	case ModeC:
		ctab->ow = cwin;
		cwin = &cmdline;
		estr_clean(&cmdline.l->s);
		break;
	case ModeV:
		mark(&ARG(.i = '\''));
		selected = 1;
		break;
	default:
		switch (omode) {
		case ModeC:
			cwin = ctab->ow;
			break;
		case ModeV:
			selected = 0;
			break;
		}
		break;
	}
}

void
movedown(const Arg *arg)
{
	selected = 0;
	if (cwin->row == 0 && arg->i < 0)
		return;
	cwin->row += arg->i;
}

void
moveright(const Arg *arg)
{
	selected = 0;
	if (cwin->col == 0 && arg->i < 0)
		return;
	cwin->col += arg->i;
}

void
newline(const Arg *arg)
{
	Edit e = {0};
	e.beg.row = cwin->row;
	if (arg->i < 0) {
		e.beg.col = 0;
		e.cursor = e.beg;
		e.setcursor = 1;
	} else {
		e.beg.col = ustrlen(cwin->l->s.s) + 1;
	}
	e.end = e.beg;
	estr_from_cstr(&e.replace, "\n");
	edit(&e);
	str_free(&e.replace);
}

void
newtab(const Arg *arg)
{
	darr_expand(&tabs);
	ctab = &darr_last(&tabs);
	memset(ctab, 0, sizeof(*ctab));
	cwin = &ctab->main;
	cwin->x = cwin->y = 0;
	cwin->h = scrh - 1;
	cwin->w = scrw;

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
	b->nline--;
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
selline(const Arg *arg)
{
	switch (arg->i) {
	case -1:
		mark(&ARG(.i = '\''));
		cwin->col = 0;
		break;
	case 0:
		cwin->col = 0;
	case 1:
		mark(&ARG(.i = '\''));
		cwin->col = ustrlen(cwin->l->s.s);
		break;
	}
	selected = 1;
}

void
seltextobj(const TextObj *t)
{
	cwin->col = t->beg.col;
	mark(&ARG(.i = '\''));
	cwin->col = t->end.col;
	selected = 1;
}

void
selword(const Arg *arg)
{
	TextObj t = {0};
	t.beg.row = cwin->row;
	t.beg.col = cwin->col;
	t.begln = cwin->l;
	if (!textobj_get(&t, arg->i))
		return;
	seltextobj(&t);
}

void
setcol(Win *w, unsigned int col)
{
	w->col = align(col, 0, ustrlen(w->l->s.s));
	if (w->col < w->coloff)
		w->coloff = w->col;
	else if (w->col >= w->coloff + scrw)
		w->coloff = w->col - scrw + 1;
}

void
setrow(Win *w, unsigned int row)
{
	w->row = align(row, 0, w->b->nline - 1);
	if (w->row < w->rowoff)
		w->rowoff = w->row;
	else if (w->row >= w->rowoff + w->h)
		w->rowoff = w->row - w->h + 1;
	w->l = getln(w->l, w->orow, w->row);
	setcol(w, w->col);
	w->orow = w->row;
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

void
update(void)
{
	setrow(&ctab->main, ctab->main.row);
	if (ctab->bottom.h)
		setrow(&ctab->bottom, ctab->bottom.row);
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

	initcmdbuf();
	cmdline.b = &cmdbuf;
	cmdline.l = lineof(cmdbuf.lines.beg);
	cmdline.x = 0;
	cmdline.y = scrh;
	cmdline.h = 1;
	cmdline.w = scrw;

	sctui_open_alt_screen();

	running = 1;
	while (running) {
		draw();
		pollev();
		handlekey();
		update();
	}

	sctui_close_alt_screen();
	sctui_commit();
	sctui_fini();

	return 0;
}

#include "textobj.c" /* sucks */
