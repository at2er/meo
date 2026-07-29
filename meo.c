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

enum { ModeN, ModeC, ModeF, ModeI, ModeV };

typedef struct Buf {
	struct utilsh_list_head lines, undos;
	unsigned int nline;
	char path[FILENAME_MAX];

	unsigned int modified:1;
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
	Line *l;
	Pos p;
	unsigned int rowoff, coloff;
} Mark;

typedef struct Win {
	Buf *b;
	Line *l;
	unsigned int row, rowoff,
	             col, coloff;
	unsigned int x, y, h, w;

	/* if you set these value without setrow() or else,
	 * __please remember__ set these value to ensure update() works correctly. */
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

typedef darr(Buf*) bufs_t;
typedef darr(struct pollfd) pfds_t;
typedef darr(Tab) tabs_t;

static char sbuf[BUFSIZ], rbuf[BUFSIZ];

#include "clipboard.h"
#include "textobj.h"

static void backspace(const Arg *arg);
static int caninsert(void);
static void change(const Arg *arg);
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
static void duptoreg(int idx, struct str *s);
static Edit *edit(const Edit *e);
static void einsert(const struct str *content);
static Line *enewline(Buf *b, Line *at);
static void eremove(struct str *backup, unsigned int beg, unsigned int end);
static void eremovem(struct str *backup, Pos *beg, Pos *end);
static void findnex(const Arg *arg);
static void findprv(const Arg *arg);
static Line *freadline(FILE *fp);
static Line *getln(Line *curl, unsigned int crow, unsigned int row);
static unsigned int getrx(Line *l, unsigned int col);
static void gotoinfile(const Arg *arg);
static void gotoinline(const Arg *arg);
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
static const char *matched;
static regmatch_t matches[1];
static regex_t pattern;
static int patterncomped;
static pfds_t pfds;
static int running;
static int selected;
static tabs_t tabs;

/* events */
static int keyev;

/* '\'': explicit selection by user
 * '"':  implicit selection like some jumping actions */
static Mark marks[UCHAR_MAX];
#define SELMARK marks['\'']

/* '"': clipboard
 * '.': last action */
static char *regs[UCHAR_MAX];
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
	Line *l = cwin->l;
	e.end.row = cwin->row;
	e.end.col = cwin->col;
	e.beg = e.end;

	if (e.beg.col == 0) {
		if (e.beg.row == 0)
			return;
		l = lineof(cwin->l->link.prv);
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

	for (tok = dup; (tok = strtok_r(tok, " \r\t\n", &saver)); tok = NULL)
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
setwin:
	ctab->main.b = b;
	ctab->main.l = lineof(b->lines.beg);
	ctab->main.row = ctab->main.orow = 0;
	ctab->main.rowoff = ctab->main.coloff = 0;
	ctab->main.col = ctab->main.ocol = 0;
}

void
cmdquit(int argc, const char *argv[])
{
	for (int i = 0; i < bufs.n; i++)
		if (bufs.e[i]->modified)
			return;
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

	cwin->b->modified = 0;
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
	e.beg.col = cwin->col;
	e.beg.row = cwin->row;
	if (selected) {
		e.end = SELMARK.p;
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
	sctui_move(cwin->x + getrx(cwin->l, cwin->col),
			cwin->y + cwin->row - cwin->rowoff);
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
	estr_append_cstr(&barbuf, cwin->b->path);
	estr_append_chr(&barbuf, ' ');
	if (cwin->b->modified)
		estr_append_chr(&barbuf, 'm');
	else
		estr_append_chr(&barbuf, '-');
	estr_append_chr(&barbuf, ' ');
	snprintf(sbuf, BUFSIZ, "%u,%u", cwin->row + 1, cwin->col + 1);
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

void
duptoreg(int idx, struct str *s)
{
	char **reg = &regs[idx];
	if (*reg)
		free(*reg);
	*reg = strndup(s->s, s->len);

	if (idx == '+')
		clipboard_set(s);
}

/* it returns a reverse edit operation,
 * __Don't free() it__ */
Edit *
edit(const Edit *e)
{
	Undo *u = newundo(cwin->b);
	Pos _beg = e->beg, _end = e->end, *beg = &_beg, *end = &_end;

	swappos(&beg, &end);

	setrow(cwin, beg->row);
	cwin->row = beg->row;
	cwin->col = beg->col;

	u->e.beg = u->e.end = *beg;
	if (beg->row == end->row)
		eremove(&u->e.replace, beg->col, end->col);
	else
		eremovem(&u->e.replace, beg, end);

	if (e->replace.s) {
		einsert(&e->replace);
		str_empty(&u->e.replace);
		u->e.end.row = cwin->row;
		u->e.end.col = cwin->col;
	}

	if (e->setcursor) {
		setrow(cwin, e->cursor.row);
		cwin->col = e->cursor.col;
	}

	if (cmode == ModeV)
		mode(&ARG(.i = ModeN));
	if (selected)
		selected = 0;

	cwin->b->modified = 1;

	return &u->e;
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
			cwin->orow = ++cwin->row;
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
eremove(struct str *backup, unsigned int beg, unsigned int end)
{
	unsigned int bbeg, bend;
	Pos fbeg, fend; /* fake */
	Line *l = cwin->l;

	if (beg == end)
		return;

	if (selected == 2 || (end > ustrlen(l->s.s) && l->link.nex)) {
		fbeg.row = cwin->row;
		fbeg.col = cwin->col;
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
	unsigned int bcol = coltobcol(cwin->l, cwin->col);
	Line *begln = cwin->l, *l, *nex;

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
		removeln(cwin->b, l);
		l = nex;
	}

	/* l == e->end.row */
	bcol = coltobcol(l, end->col);
	if (bcol < l->s.len)
		estr_append_cstr(&begln->s, l->s.s + bcol);
	if (backup)
		estr_append_str(backup, &STR(l->s.s, bcol));
	removeln(cwin->b, l);
}

void
findnex(const Arg *arg)
{
	uint_least32_t cp;
	Line *oln = cwin->l;
	int k = arg->i;
	size_t ret, off;

	if (k == 0)
		k = pollkey();

	mark(&ARG(.i = '\''));
	selected = 1;

	off = coltobcol(cwin->l, cwin->col);
	while (1) {
		grapheme_decode_iter(cwin->l->s.s, ret, off, cp) {
			if ((int)cp == k)
				goto found;
			cwin->col++;
		}
		if (!findpassthrough || cwin->row >= cwin->b->nline - 1) {
			cwin->col = cwin->ocol;
			cwin->row = cwin->orow;
			cwin->l = oln;
			return;
		}
		off = 0;
		cwin->row++;
		cwin->col = 0;
		cwin->l = lineof(cwin->l->link.nex);
	}
found:
	cwin->orow = cwin->row;
	cwin->ocol = ++cwin->col;
}

void
findprv(const Arg *arg)
{
	unsigned int col, found;
	uint_least32_t cp;
	Line *oln = cwin->l;
	int k = arg->i;
	size_t ret, off;

	if (k == 0)
		k = pollkey();

	mark(&ARG(.i = '\''));
	selected = 1;

	while (1) {
		cwin->col = found = off = 0;
		grapheme_decode_iter(cwin->l->s.s, ret, off, cp) {
			if ((int)cp == k) {
				col = cwin->col;
				found = 1;
			}
			if (cwin->col >= cwin->ocol && cwin->row == cwin->orow) {
				if (found)
					goto found;
				break;
			}
			cwin->col++;
		}
		if (found && cwin->row != cwin->orow)
			goto found;
		found = 0;
		if (!findpassthrough || cwin->row == 0) {
			cwin->col = cwin->ocol;
			cwin->row = cwin->orow;
			cwin->l = oln;
			return;
		}
		cwin->row--;
		cwin->col = 0;
		cwin->l = lineof(cwin->l->link.prv);
	}
found:
	cwin->orow = cwin->row;
	cwin->ocol = cwin->col = col;
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
		cwin->row = 0;
	else
		cwin->row = cwin->b->nline - 1;
}

void
gotoinline(const Arg *arg)
{
	switch (arg->i) {
	case -1:
		mark(&ARG(.i = '\''));
		cwin->col = 0;
		break;
	case 1:
		mark(&ARG(.i = '\''));
		cwin->col = ustrlen(cwin->l->s.s);
		break;
	}
	selected = 1;
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
	cwin->b = m->b;
	cwin->row = cwin->orow = m->p.row;
	cwin->col = cwin->ocol = m->p.col;
	cwin->rowoff = m->rowoff;
	cwin->coloff = m->coloff;
	cwin->l = m->l;
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

	li->l = li->nex = getln(cwin->l, cwin->row, li->beg.row);
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
	m->b = cwin->b;
	m->l = cwin->l;
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
	case ModeC: case ModeF:
		ctab->ow = cwin;
		cwin = &cmdline;
		cwin->col = 0;
		estr_clean(&cmdline.l->s);
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
	if (arg->i < 0 && cwin->row < (unsigned int)-arg->i)
		cwin->row = 0;
	else
		cwin->row += arg->i;
	if (cwin->row >= cwin->b->nline)
		cwin->row = cwin->b->nline - 1;
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
	unsigned int off = 0, orow = cwin->row;
	Line *oln = cwin->l;

	if (!patterncomped)
		return;

	if (!(matched && RANGE(matched, cwin->l->s.s, cwin->l->s.s + cwin->l->s.len)))
		matched = cwin->l->s.s;
	while (1) { 
		if (!regexec(&pattern, matched, 1, matches, 0))
			goto matched;
		movedown(&ARG(.i = arg->i));
		if (cwin->orow == cwin->row)
			break;
		cwin->l = getln(cwin->l, cwin->orow, cwin->row);
		cwin->orow = cwin->row;
		matched = cwin->l->s.s;
	}

	matched = NULL;
	cwin->row = cwin->orow = orow;
	cwin->l = oln;
	return;
matched:
	beg.row = end.row = cwin->row;
	if (matched != cwin->l->s.s)
		off = matched - cwin->l->s.s;
	beg.col = bcoltocol(cwin->l, matches[0].rm_so + off);
	end.col = bcoltocol(cwin->l, matches[0].rm_eo + off);
	sel(&beg, &end);
	matched = cwin->l->s.s + matches[0].rm_eo + off;
}

void
paste(const Arg *arg)
{
	Edit e = {0};
	char *str = regs[arg->i];
	struct str tmp;
	if (arg->i == '+' && !clipboard_get(&tmp))
		str = tmp.s;

	if (cmode != ModeV && str) {
		insert(&ARG(.s = str));
		return;
	}

	e.beg.row = cwin->row;
	e.beg.col = cwin->col;
	e.end = SELMARK.p;
	e.replace.s = str;
	if (str)
		e.replace.len = strlen(str);
	edit(&e);
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
	draw();
	while (!keyev)
		pollev();
	return keyev;
}

void
redo(const Arg *arg)
{
	Edit e;
	struct utilsh_list_head *undos = &cwin->b->undos;
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

	if (arg->s) {
		dup = strdup(arg->s);
	} else if (cmode == ModeF) {
		mode(&ARG(.i = ModeN));
		if (!cmdline.l->s.s[0])
			return;
		if (!dup)
			dup = strdup(cmdline.l->s.s);
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
	cwin->col = 0;
	mark(&ARG(.i = '\''));
	cwin->col = ustrlen(cwin->l->s.s);
	selected = 2; /* to delete the whole line */
}

void
selword(const Arg *arg)
{
	unsigned int lstart, lend;
	TextObj t = {0};
	t.beg.row = cwin->row;
	t.beg.col = cwin->col;
	t.begln = cwin->l;
	if (!textobj_get(&t, arg->i))
		return;
	sel(&t.beg, &t.end);
	lstart = coltobcol(cwin->l, t.beg.col);
	lend = coltobcol(cwin->l, t.end.col);
	search(&ARG(.s = strndup(cwin->l->s.s + lstart, lend - lstart)));
}

void
setcol(Win *w, unsigned int col)
{
	w->col = align(col, 0, ustrlen(w->l->s.s));
	if (w->col < w->coloff)
		w->coloff = w->col;
	else if (w->col >= w->coloff + scrw)
		w->coloff = w->col - scrw + 1;
	w->ocol = w->col;
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
undo(const Arg *arg)
{
	Edit e; /* a copy, we need */
	struct utilsh_list *prv;
	struct utilsh_list_head *undos = &cwin->b->undos;
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
		setrow(&t->bottom, t->bottom.row);
	setrow(&t->main, t->main.row);
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

	if (!selected || cwin->b != SELMARK.b)
		return;

	str_empty(&tmp);

	beg = SELMARK.p;
	end.row = cwin->row;
	end.col = cwin->col;
	makelniter(&iter, &beg, &end);
	while (iterln(&iter)) {
		lstart = coltobcol(iter.l, iter.lstart);
		lend = coltobcol(iter.l, iter.lend);
		estr_append_str(&tmp, &STR(iter.l->s.s + lstart, lend - lstart));
		if (beg.row == end.row)
			break;
		if (lend >= iter.l->s.len)
			estr_append_chr(&tmp, '\n');
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
	cmdline.b = &cmdbuf;
	cmdline.l = lineof(cmdbuf.lines.beg);
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
