#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
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
#define LISTEN_SIG(SIG, FLAGS, HANDLER) \
		sigaction(SIG, &(struct sigaction){ \
			.sa_flags = FLAGS, \
			.sa_handler = HANDLER \
		}, NULL)

#define grapheme_decode_iter(STR, RET, OFF, CP) \
	for (; (RET = grapheme_decode_utf8((STR) + OFF, SIZE_MAX, &(CP))) > 0 \
				&& (CP) != 0; \
			OFF += RET)

#define lineof(LINK) utilsh_list_container_of(LINK, Line, link)
#define undoof(LINK) utilsh_list_container_of(LINK, Undo, link)
#define marktopos(POS, MARK) do { \
		(POS)->row = (MARK)->row; \
		(POS)->col = (MARK)->col; \
	} while (0);

enum { ModeN, ModeC, ModeF, ModeI, ModeV };

typedef struct Mark {
	struct Buf *b;
	struct Line *l;
	unsigned int row, col;
	unsigned int rowoff, coloff;
} Mark;

typedef struct Buf {
	struct utilsh_list_head lines, undos;
	unsigned int nline;
	char path[FILENAME_MAX];

	unsigned int ldirty:1, modified:1;

	Mark pos;
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

typedef struct Win {
	Mark p;
	unsigned int x, y, h, w;

	/* if you set these value without setrow() or else,
	 * __please remember__ set these value to ensure update() works correctly. */
	unsigned int orow, ocol;
} Win;
typedef struct Tab {
	Win main, bottom;
	Win *w, *ow;

	Buf *prvb;
} Tab;

typedef struct Uchr {
	int w, blen;
	uint_least32_t cp;
} Uchr;

typedef struct Undo {
	Edit e;
	struct utilsh_list link;
} Undo;

typedef darr(Buf*) bufs_t;
typedef darr(struct pollfd) pfds_t;
typedef darr(Tab) tabs_t;

static char sbuf[BUFSIZ], rbuf[BUFSIZ];

#include "clipboard.h"
#include "textobj.h"

static void backspace(const Arg *arg);
static int caninsert(void);
static void change(const Arg *arg);
static void changebuf(Tab *t, Buf *b);
static void cmd(const Arg *arg);
static void cmdbuffer(int argc, const char *argv[]);
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
static void duptoreg(int idx, struct str *s);
static Edit *edit(const Edit *e);
static void einsert(const struct str *content);
static Line *enewline(Buf *b, Line *at);
static void eremove(struct str *backup, unsigned int beg, unsigned int end);
static void eremovem(struct str *backup, Pos *beg, Pos *end);
static void execreg(const Arg *arg);
static void findnex(const Arg *arg);
static void findprv(const Arg *arg);
static Line *freadline(FILE *fp);
static Line *getln(Line *curl, unsigned int crow, unsigned int row);
static unsigned int getrx(Line *l, unsigned int col);
static void gotoinfile(const Arg *arg);
static void gotoinline(const Arg *arg);
static void gotoline(const Arg *arg);
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
static Undo *newundo(Buf *b);
static void nexmatch(const Arg *arg);
static void paste(const Arg *arg);
static void pollev(void);
static int pollkey(void);
static int pollkeyev(void); /* poll key event only */
static void record(const Arg *arg);
static void redo(const Arg *arg);
static void removeln(Buf *b, Line *l);
static size_t renderchr(Uchr *uc, const char *s);
static void resize(int sig);
static void search(const Arg *arg);
static void sel(Pos *beg, Pos *end);
static void selline(const Arg *arg);
static void selword(const Arg *arg);
static void setcol(Win *w, unsigned int col);
static void setrow(Win *w, unsigned int row);
static void suspend(const Arg *arg);
static void swappos(Pos **beg, Pos **end);
static void tillnex(const Arg *arg);
static void tillprv(const Arg *arg);
static void undo(const Arg *arg);
static void update(Tab *t);
static void updatewins(Tab *t);
static void yank(const Arg *arg);

static struct str barbuf;
static bufs_t bufs;
static Buf cmdbuf;
static Win cmdline;
static int cmode = ModeN;
static Tab *ctab;
#define cwin (ctab->w)
#define cpos (cwin->p)
static const char *matched;
static regmatch_t matches[1];
static regex_t pattern;
static int patterncomped;
static pfds_t pfds;
static int running, forcequit;
static int selected;
static tabs_t tabs;

static int recording, executing;
static size_t executingpos;

/* events */
static int keyev;

/* '\'': explicit selection by user
 * '"':  implicit selection like some jumping actions */
static Mark marks[UCHAR_MAX];
#define SELMARK marks['\'']

/* '"': clipboard
 * '.': last action */
static struct str regs[UCHAR_MAX];
#define CLIPREG regs['"']
#define DOTREG  regs['.']

static struct option opts[] = {
	OPT_END
};

#include "config.h"

void
backspace(const Arg *arg)
{
	Edit e = {0};
	Line *l = cpos.l;
	marktopos(&e.end, &cpos);
	e.beg = e.end;

	if (e.beg.col == 0) {
		if (e.beg.row == 0)
			return;
		l = lineof(cpos.l->link.prv);
		e.beg.row--;
		e.beg.col = ustrlen(l->s.s);
	} else {
		e.beg.col--;
	}

	edit(&e);
}

unsigned int
bcoltocol(Line *l, unsigned int bcol)
{
	unsigned int col = 0;
	for (unsigned int off = 0; off < bcol && l->s.s[off]; col++)
		off += grapheme_next_character_break_utf8(l->s.s + off, SIZE_MAX);
	return col;
}

int
caninsert(void)
{
	switch (cmode) {
	case ModeC: case ModeF: case ModeI:
		return 1;
	default:
		return 0;
	}
}

void
change(const Arg *arg)
{
	if (selected == 2)
		selected = 1;
	delete(arg);
	mode(&ARG(.i = ModeI));
}

void
changebuf(Tab *t, Buf *b)
{
	if ((t->prvb = t->main.p.b))
		t->prvb->pos = t->main.p;
	t->main.p = b->pos;
	t->main.orow = b->pos.row;
	t->main.ocol = b->pos.col;
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
		if (!cmdline.p.l->s.s[0])
			return;
		if (!dup)
			dup = strdup(cmdline.p.l->s.s);
	}

	if (!dup)
		return;

	darr_init(&args);

	for (tok = dup; (tok = strtok_r(tok, " \r\t\n", &saver)); tok = NULL)
		darr_append(&args, tok);

	darr_append(&args, NULL);
	args.n--;

	if (!(c = matchcmd(args.e[0]))) {
		gotoline(&ARG(.s = args.e[0]));
		return;
	}

	c->func(args.n, (const char **)args.e);
}

void
cmdbuffer(int argc, const char *argv[])
{
	Buf *b = NULL;
	int idx;

	if (argc <= 1) {
		//listbuffers();
		return;
	}

	if (argc > 1 && argv[1]) {
		if (*argv[1] == '#' && ctab->prvb) {
			b = ctab->prvb;
			goto chbuf;
		}
		if ((idx = atoi(argv[1])) >= bufs.n)
			return;
		if (idx < 0)
			return;
		b = bufs.e[idx];
	}

	if (!b)
		return;
chbuf:
	changebuf(ctab, b);
}

void
cmdedit(int argc, const char *argv[])
{
	Buf *b;
	FILE *fp;
	Line *l;

	if (argc > 1) {
		strmatch(argv[1], int, i, bufs.n, bufs.e[i]->path) {
			b = bufs.e[i];
			goto setwin;
		}
	}

	b = ecalloc(1, sizeof(*b));
	list_init(&b->lines);
	list_init(&b->undos);

	if (argc > 1) {
		strcpy(b->path, argv[1]);
		if ((fp = fopen(argv[1], "r"))) {
			for (; (l = freadline(fp)); b->nline++)
				list_insert(&b->lines, b->lines.end, &l->link);
		}
	} else {
		strcpy(b->path, "<unnamed>");
	}

	if (!b->lines.beg) {
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->s, "");
		list_insert(&b->lines, b->lines.end, &l->link);
		b->nline = 1;
	}

	darr_append(&bufs, b);
	b->pos.l = lineof(b->lines.beg);
setwin:
	b->pos.b = b;
	changebuf(ctab, b);
}

void
cmdquit(int argc, const char *argv[])
{
	if (forcequit) {
		running = 0;
		return;
	}
	for (int i = 0; i < bufs.n; i++) {
		if (bufs.e[i]->modified) {
			forcequit = 1;
			return;
		}
	}
	running = 0;
}

void
cmdwrite(int argc, const char *argv[])
{
	FILE *fp;
	const char *path;

	if (argc <= 1 || !argv[1])
		path = cpos.b->path;
	else
		path = argv[1];

	if (!(fp = fopen(path, "w")))
		return;

	list_for_each(Line, l, cpos.b->lines.beg, tmp, link) {
		fputs(l->s.s, fp);
		fputc('\n', fp);
	}

	fclose(fp);

	cpos.b->modified = 0;
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
	Edit e = {0}, *ue;
	marktopos(&e.beg, &cpos);
	if (selected) {
		marktopos(&e.end, &SELMARK);
	} else {
		e.end = e.beg;
		e.end.col++;
	}
	ue = edit(&e);
	duptoreg(arg->i, &ue->replace);
}

void
draw(void)
{
	if (cmode == ModeC || cmode == ModeF)
		drawcmdline();
	else
		drawbar();
	drawwin(&ctab->main);
	if (ctab->bottom.h)
		drawwin(&ctab->bottom);
	sctui_move(cwin->x + getrx(cpos.l, cpos.col),
			cwin->y + cpos.row - cpos.rowoff);
	sctui_commit();
}

void
drawbar(void)
{
	int i;
	size_t off, ret;
	Uchr uc;

	estr_append_cstr(&barbuf, modestr[cmode]);
	estr_append_chr(&barbuf, ' ');
	if (recording) {
		strcpy(sbuf, "[@_] ");
		sbuf[2] = recording;
		estr_append_cstr(&barbuf, sbuf);
	}
	estr_append_cstr(&barbuf, cpos.b->path);
	estr_append_chr(&barbuf, ' ');
	if (cpos.b->modified)
		estr_append_chr(&barbuf, 'm');
	else
		estr_append_chr(&barbuf, '-');
	estr_append_chr(&barbuf, ' ');
	snprintf(sbuf, BUFSIZ, "%u,%u", cpos.row + 1, cpos.col + 1);
	estr_append_cstr(&barbuf, sbuf);
	estr_append_chr(&barbuf, ' ');
	for (i = 0; i < skb_ncombo; i++)
		sbuf[i] = (char)skb_combo[i];
	sbuf[i] = '\0';
	for (off = 0; (ret = renderchr(&uc, sbuf + off)) > 0; off += ret)
		estr_append_str(&barbuf, &STR(sbuf + off, uc.blen));

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
	Line *d = getln(w->p.l, w->p.row, w->p.rowoff);
	LineIter iter;
	unsigned int nl;
	int s = selected && w->p.b == SELMARK.b, selbeg, selend;
	Pos _beg, _end, *beg = &_beg, *end = &_end;
	marktopos(&_beg, &SELMARK);

	if (s) {
		_end.row = w->p.row;
		_end.col = w->p.col;
		swappos(&beg, &end);
		iter.beg = *beg;
		iter.end = *end;
		iter.lrow = w->p.rowoff;
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

void
duptoreg(int idx, struct str *s)
{
	struct str *reg = &regs[idx];
	str_free(reg);
	estr_from_str(reg, s);

	if (idx == '+')
		clipboard_set(s);
}

/* it returns a reverse edit operation,
 * __Don't free() it__ */
Edit *
edit(const Edit *e)
{
	Undo *u = newundo(cpos.b);
	Pos _beg = e->beg, _end = e->end, *beg = &_beg, *end = &_end;

	swappos(&beg, &end);

	setrow(cwin, beg->row);
	cpos.row = beg->row;
	cpos.col = beg->col;

	u->e.beg = u->e.end = *beg;
	if (beg->row == end->row)
		eremove(&u->e.replace, beg->col, end->col);
	else
		eremovem(&u->e.replace, beg, end);

	if (e->replace.s) {
		einsert(&e->replace);
		str_empty(&u->e.replace);
		marktopos(&u->e.end, &cpos);
	}

	if (e->setcursor) {
		setrow(cwin, e->cursor.row);
		cpos.col = e->cursor.col;
	}

	if (cmode == ModeV)
		mode(&ARG(.i = ModeN));
	if (selected)
		selected = 0;

	cpos.b->modified = 1;

	return &u->e;
}

void
einsert(const struct str *content)
{
	unsigned int bcol = coltobcol(cpos.l, cpos.col);
	const char *s = content->s;
	struct str tmp, save = {0};

	estr_from_cstr(&save, cpos.l->s.s + bcol);
	estr_remove(&cpos.l->s, bcol, cpos.l->s.len - bcol);

	while ((s = iterstr(&tmp, s))) {
		if (tmp.len == 1 && tmp.s[0] == '\n') {
			cwin->orow = ++cpos.row;
			cpos.col = 0;
			cpos.l = enewline(cpos.b, cpos.l);
		} else {
			estr_append_str(&cpos.l->s, &tmp);
			cpos.col += tmp.len;
		}
	}

	if (save.s)
		estr_append_str(&cpos.l->s, &save);

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
	estr_from_cstr(&l->s, "");
	return l;
}

void
eremove(struct str *backup, unsigned int beg, unsigned int end)
{
	unsigned int bbeg, bend;
	Pos fbeg, fend; /* fake */
	Line *l = cpos.l;

	if (beg == end)
		return;

	if (selected == 2 || (end > ustrlen(l->s.s) && l->link.nex)) {
		fbeg.row = cpos.row;
		fbeg.col = cpos.col;
		fend.row = fbeg.row + 1;
		fend.col = 0;
		eremovem(backup, &fbeg, &fend);
		return;
	}
	bbeg = coltobcol(l, beg);
	bend = coltobcol(l, end);
	if (backup)
		estr_from_str(backup, &STR(l->s.s + bbeg, bend - bbeg));
	estr_remove(&l->s, bbeg, bend - bbeg);
}

void
eremovem(struct str *backup, Pos *beg, Pos *end)
{
	unsigned int bcol = coltobcol(cpos.l, cpos.col);
	Line *begln = cpos.l, *l, *nex;

	if (backup) {
		estr_from_str(backup, &STR(begln->s.s + bcol, begln->s.len - bcol));
		estr_append_chr(backup, '\n');
	}
	estr_remove(&begln->s, bcol, begln->s.len - bcol);

	l = nex = lineof(begln->link.nex);
	for (unsigned int lrow = beg->row + 1; lrow != end->row; lrow++) {
		nex = lineof(l->link.nex);
		if (backup) {
			estr_append_str(backup, &l->s);
			estr_append_chr(backup, '\n');
		}
		removeln(cpos.b, l);
		l = nex;
	}

	if (!l) {
		cpos.l = lineof(begln->link.prv);
		cwin->orow = --cpos.row;
		cwin->ocol = cpos.col = ustrlen(cpos.l->s.s);
		l = begln;
		removeln(cpos.b, l);
		return;
	}

	/* l == e->end.row */
	bcol = coltobcol(l, end->col);
	if (bcol < l->s.len)
		estr_append_cstr(&begln->s, l->s.s + bcol);
	if (backup)
		estr_append_str(backup, &STR(l->s.s, bcol));
	removeln(cpos.b, l);
}

void
execreg(const Arg *arg)
{
	int k = arg->i;
	if (!k)
		k = pollkey();
	executing = k;
	executingpos = 0;
}

void
findnex(const Arg *arg)
{
	int k = arg->i;
	TextObj t = {0};

	if (k == 0)
		k = pollkey();

	marktopos(&t.beg, &cpos);
	t.begln = cpos.l;
	if (!textobj_find_nex(k, &t))
		return;

	sel(&t.abeg, &t.aend);
}

void
findprv(const Arg *arg)
{
	int k = arg->i;
	TextObj t = {0};

	if (k == 0)
		k = pollkey();

	marktopos(&t.beg, &cpos);
	t.begln = cpos.l;
	if (!textobj_find_prv(k, &t))
		return;

	sel(&t.abeg, &t.aend);
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
gotoinfile(const Arg *arg)
{
	selected = 0;
	if (arg->i < 0)
		cpos.row = 0;
	else
		cpos.row = cpos.b->nline - 1;
}

void
gotoinline(const Arg *arg)
{
	switch (arg->i) {
	case -1:
		mark(&ARG(.i = '\''));
		cpos.col = 0;
		break;
	case 1:
		mark(&ARG(.i = '\''));
		cpos.col = ustrlen(cpos.l->s.s);
		break;
	}
	selected = 1;
}

void
gotoline(const Arg *arg)
{
	const char *c = arg->s;
	unsigned int row = 0;
	for (; *c; c++) {
		if (*c < '0' || *c > '9')
			return;
		row *= 10;
		row += *c - '0';
	}
	if (c != arg->s)
		setrow(cwin, row == 0 ? row : row - 1);
}

void
gotomark(const Arg *arg)
{
	int k = arg->i;
	Mark *m;
	if (k == 0)
		k = pollkey();
	m = &marks[k];
	if (!m->b)
		return;
	cpos = *m;
	if (m->b->ldirty)
		cpos.l = m->l = getln(lineof(m->b->lines.beg), 0, m->row);
	cwin->orow = m->row;
	cwin->ocol = m->col;
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
	marktopos(&e.beg, &cpos);
	e.end = e.beg;
	e.replace.s = (char *)arg->s;
	e.replace.len = strlen(arg->s);

	edit(&e);
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
		if (li->end.col == 0)
			return NULL;
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
	if (selected == 2)
		li->lend += 1;
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

	li->l = li->nex = getln(cpos.l, cpos.row, li->beg.row);
	li->lrow = li->beg.row;
}

void
mark(const Arg *arg)
{
	int k = arg->i;
	Mark *m;
	if (k == 0)
		k = pollkey();
	m = &marks[k];
	*m = cpos;
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
	case ModeC: case ModeF:
		ctab->ow = cwin;
		cwin = &cmdline;
		cpos.col = 0;
		estr_clean(&cmdline.p.l->s);
		break;
	case ModeV:
		if (!selected)
			mark(&ARG(.i = '\''));
		break;
	default:
		switch (omode) {
		case ModeC: case ModeF:
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
	if (arg->i < 0 && cpos.row < (unsigned int)-arg->i)
		cpos.row = 0;
	else
		cpos.row += arg->i;
	if (cpos.row >= cpos.b->nline)
		cpos.row = cpos.b->nline - 1;
}

void
moveright(const Arg *arg)
{
	selected = 0;
	if (cpos.col == 0 && arg->i < 0)
		return;
	cpos.col += arg->i;
}

void
newline(const Arg *arg)
{
	Edit e = {0};
	e.beg.row = cpos.row;
	if (arg->i < 0) {
		e.beg.col = 0;
		e.cursor = e.beg;
		e.setcursor = 1;
	} else {
		e.beg.col = ustrlen(cpos.l->s.s) + 1;
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
		cmdedit(ARGCV("e"));
	else
		cmdedit(ARGCV("e", arg->s));
}

Undo *
newundo(Buf *b)
{
	Undo *u;

	if (b->undos.end) {
		if (b->undos.end->nex) {
			b->undos.end = b->undos.end->nex;
			goto set;
		}
	} else {
		if (b->undos.beg) {
			b->undos.end = b->undos.beg;
			goto set;
		}
	}

	u = ecalloc(1, sizeof(*u));
	list_insert(&b->undos, b->undos.end, &u->link);
	return u;
set:
	return undoof(b->undos.end);
}

void
nexmatch(const Arg *arg)
{
	Pos beg, end;
	unsigned int off = 0, orow = cpos.row;
	Line *oln = cpos.l;

	if (!patterncomped)
		return;

	if (!(matched && RANGE(matched, cpos.l->s.s, cpos.l->s.s + cpos.l->s.len)))
		matched = cpos.l->s.s;
	while (1) { 
		if (!regexec(&pattern, matched, 1, matches, 0))
			goto matched;
		movedown(&ARG(.i = arg->i));
		if (cwin->orow == cpos.row)
			break;
		cpos.l = getln(cpos.l, cwin->orow, cpos.row);
		cwin->orow = cpos.row;
		matched = cpos.l->s.s;
	}

	matched = NULL;
	cpos.row = cwin->orow = orow;
	cpos.l = oln;
	return;
matched:
	beg.row = end.row = cpos.row;
	if (matched != cpos.l->s.s)
		off = matched - cpos.l->s.s;
	beg.col = bcoltocol(cpos.l, matches[0].rm_so + off);
	end.col = bcoltocol(cpos.l, matches[0].rm_eo + off);
	sel(&beg, &end);
	matched = cpos.l->s.s + matches[0].rm_eo + off;
}

void
paste(const Arg *arg)
{
	Edit e = {0};
	char *str = regs[arg->i].s;
	struct str tmp;
	if (arg->i == '+' && !clipboard_get(&tmp))
		str = tmp.s;

	if (cmode != ModeV && str) {
		insert(&ARG(.s = str));
		return;
	}

	marktopos(&e.beg, &cpos);
	marktopos(&e.end, &SELMARK);
	e.replace.s = str;
	if (str)
		e.replace.len = strlen(str);
	edit(&e);
}

void
pollev(void)
{
	keyev = 0;

	if (executing) {
		keyev = regs[executing].s[executingpos++];
		if (executingpos >= regs[executing].len)
			executing = 0;
		/* skip poll(), because current [keyev] need handle. */
		return;
	}

	if (poll(pfds.e, pfds.n, -1) == -1 && errno != EINTR)
		die("poll()");

	keyev = pollkeyev();
}

int
pollkey(void)
{
	keyev = 0;
	draw();
	while (!keyev)
		pollev();
	return keyev;
}

int
pollkeyev(void)
{
	struct str *reg;

	if (pfds.e[0].revents & POLLIN) {
		keyev = sctui_grab_key();
		if (recording) {
			reg = &regs[recording];
			estr_append_chr(reg, keyev);
		}
	}

	return keyev;
}

void
record(const Arg *arg)
{
	int k = arg->i;
	if (recording) {
		estr_remove(&regs[recording], regs[recording].len - 1, 1);
		recording = 0;
		return;
	}
	if (!k)
		k = pollkey();
	recording = k;
	str_clean(&regs[recording]);
}

void
redo(const Arg *arg)
{
	Edit e;
	struct utilsh_list_head *undos = &cpos.b->undos;
	Undo *u;

	if (undos->end) {
		if (!undos->end->nex)
			return;
		u = undoof(undos->end->nex);
	} else {
		if (!undos->beg)
			return;
		u = undoof(undos->beg);
	}

	e = u->e;
	edit(&e);
	undos->end = &u->link;
}

void
removeln(Buf *b, Line *l)
{
	list_remove(&b->lines, &l->link);
	str_free(&l->s);
	free(l);
	b->ldirty = 1;
	b->nline--;
}

size_t
renderchr(Uchr *uc, const char *s)
{
	size_t ret = grapheme_decode_utf8(s, SIZE_MAX, &uc->cp);
	char *sb = rbuf;

	if (!ret || !uc->cp)
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
resize(int sig)
{
	sctui_update();
	cmdline.y = scrh;
	cmdline.w = scrw;
	for (int i = 0; i < tabs.n; i++)
		updatewins(&tabs.e[i]);
}

/* I don't know why call it "search" but the mode is "find" */
void
search(const Arg *arg)
{
	char *dup = NULL;
	int ret;

	if (arg->s)
		dup = strdup(arg->s);
	if (cmode == ModeF) {
		mode(&ARG(.i = ModeN));
		if (!cmdline.p.l->s.s[0])
			return;
		if (!dup)
			dup = strdup(cmdline.p.l->s.s);
	}

	patterncomped = 0;
	ret = regcomp(&pattern, dup, 0);
	free(dup);
	if (ret)
		return;
	patterncomped = 1;
}

void
sel(Pos *beg, Pos *end)
{
	setrow(cwin, beg->row);
	setcol(cwin, beg->col);
	mark(&ARG(.i = '\''));
	setrow(cwin, end->row);
	setcol(cwin, end->col);
	selected = 1;
}

void
selline(const Arg *arg)
{
	cpos.col = 0;
	mark(&ARG(.i = '\''));
	cpos.col = ustrlen(cpos.l->s.s);
	selected = 2; /* to delete the whole line */
}

void
selword(const Arg *arg)
{
	unsigned int lstart, lend;
	TextObj t = {0};
	marktopos(&t.beg, &cpos);
	t.begln = cpos.l;
	if (!textobj_get(&t, arg->i))
		return;
	sel(&t.abeg, &t.aend);
	lstart = coltobcol(cpos.l, t.abeg.col);
	lend = coltobcol(cpos.l, t.aend.col);
	search(&ARG(.s = strndup(cpos.l->s.s + lstart, lend - lstart)));
	matched = cpos.l->s.s + lend;
}

void
setcol(Win *w, unsigned int col)
{
	w->p.col = align(col, 0, ustrlen(w->p.l->s.s));
	if (w->p.col < w->p.coloff)
		w->p.coloff = w->p.col;
	else if (w->p.col >= w->p.coloff + scrw)
		w->p.coloff = w->p.col - scrw + 1;
	w->ocol = w->p.col;
}

void
setrow(Win *w, unsigned int row)
{
	w->p.row = align(row, 0, w->p.b->nline - 1);
	if (w->p.row < w->p.rowoff)
		w->p.rowoff = w->p.row;
	else if (w->p.row >= w->p.rowoff + w->h)
		w->p.rowoff = w->p.row - w->h + 1;
	w->p.l = getln(w->p.l, w->orow, w->p.row);
	setcol(w, w->p.col);
	w->orow = w->p.row;
}

void
suspend(const Arg *arg)
{
	sctui_fini();
	sctui_close_alt_screen();
	sctui_commit();
	kill(0, SIGSTOP);
	sctui_init();
	sctui_open_alt_screen();
	resize(0);
	sctui_commit();
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
tillnex(const Arg *arg)
{
	int k = arg->i;
	TextObj t = {0};

	if (k == 0)
		k = pollkey();

	marktopos(&t.beg, &cpos);
	t.begln = cpos.l;
	if (!textobj_find_nex(k, &t))
		return;

	sel(&t.ibeg, &t.iend);
}

void
tillprv(const Arg *arg)
{
	int k = arg->i;
	TextObj t = {0};

	if (k == 0)
		k = pollkey();

	marktopos(&t.beg, &cpos);
	t.begln = cpos.l;
	if (!textobj_find_prv(k, &t))
		return;

	sel(&t.ibeg, &t.iend);
}

void
undo(const Arg *arg)
{
	Edit e; /* a copy, we need */
	struct utilsh_list *prv;
	struct utilsh_list_head *undos = &cpos.b->undos;
	Undo *u;

	if (!undos->end)
		return;

	u = undoof(undos->end);
	undos->end = prv = u->link.prv;

	/* [u->e] will be used by newundo() in edit(), so just copy it */
	e = u->e;
	edit(&e);

	undos->end = prv;
}

void
update(Tab *t)
{
	if (t->bottom.h)
		setrow(&t->bottom, t->bottom.p.row);
	setrow(&t->main, t->main.p.row);
}

void
updatewins(Tab *t)
{
	t->main.w = t->bottom.w = scrw;
	t->main.h = scrh - 1;
	if (t->bottom.h) {
		t->bottom.h = t->main.h / 2;
		t->main.h -= t->bottom.h;
		t->bottom.y = t->main.h;
	}
}

void
yank(const Arg *arg)
{
	Pos beg, end;
	LineIter iter;
	unsigned int lstart, lend;
	struct str tmp;

	if (!selected || cpos.b != SELMARK.b)
		return;

	str_empty(&tmp);

	marktopos(&beg, &SELMARK);
	marktopos(&end, &cpos);
	makelniter(&iter, &beg, &end);
	while (iterln(&iter)) {
		lstart = coltobcol(iter.l, iter.lstart);
		lend = coltobcol(iter.l, iter.lend);
		estr_append_str(&tmp, &STR(iter.l->s.s + lstart, lend - lstart));
		if (selected == 2 || lend >= iter.l->s.len)
			estr_append_chr(&tmp, '\n');
		if (beg.row == end.row)
			break;
	}

	duptoreg(arg->i, &tmp);
}

int
main(int argc, char *argv[])
{
	char *entry = NULL;
	enum GETARG_RESULT r;
	struct sigaction sa;

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

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	while (waitpid(-1, NULL, WNOHANG) > 0);

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
	cmdline.p.b = &cmdbuf;
	cmdline.p.l = lineof(cmdbuf.lines.beg);
	cmdline.x = 0;
	cmdline.y = scrh;
	cmdline.h = 1;
	cmdline.w = scrw;

	sctui_open_alt_screen();

	LISTEN_SIG(SIGWINCH, SA_RESTART, resize);

	running = 1;
	while (running) {
		draw();
		pollev();
		handlekey();
		/* keep [selected] */
		if (cmode == ModeV)
			selected = 1;
		update(ctab);
	}

	sctui_close_alt_screen();
	sctui_commit();
	sctui_fini();

	return 0;
}

#include "textobj.c" /* sucks */
