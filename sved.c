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

#include "getarg.h"
#include "sctui.h"
#include "sved.h"
#include "utils.h"

#define ARG(...) (union arg){__VA_ARGS__}

static void draw(void);
static void draw_win(struct win *w);
static void fini(void);
static const struct key *get_keys_table(void);
static void init(void);
static void keypress(void);
static void render_line(struct line *l);

static const char *usages[] = {
"Usage: sved [OPTIONS] [FILE]",
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

/* state */
static int         cmode = 'n';
static struct tab *ctab;

static struct pollfd gfds[1];
static char gsbuf[BUFSIZ];
static int running = 1;

static const char *entry;

#include "config.h"

void
draw(void)
{
	for (int i = 0; i < nwin; i++)
		draw_win(&wins[i]);
	sctui_commit();
}

void
draw_win(struct win *w)
{
	int i = 0;

	if (!w->refresh)
		return;

	utilsh_list_for_each(struct line, l, w->fb->lines.beg, tmp, link) {
		render_line(l);
		sctui_text(w->x, w->y + i, l->r, w->w);
		i++;
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
	case 'i':
		return insert_keys;
	default:
		break;
	}

	die("get_keys_table()");

	return NULL;
}

void
init(void)
{
	sctui_init();
	gfds[0].fd = STDIN_FILENO;
	gfds[0].events = POLLIN;

	nwin++;
	wins = erealloc(wins, sizeof(*wins) * nwin);

	ntab++;
	tabs = erealloc(tabs, sizeof(*tabs) * ntab);
	ctab = tabs;
	ctab->w = wins;
	ctab->w->w = global_sctui.w;
	ctab->w->h = global_sctui.h;

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

	if (!(gfds[0].revents & POLLIN))
		return;

	k = sctui_grab_key();
	if (skb_handle_key(k)) {
		if (cmode == 'i')
			return;
		skb_ncombo = 0;
		return;
	}
	if (cmode != 'i')
		return;

	buf = gsbuf;
	for (int i = 0; i < skb_ncombo; i++) {
		if (iscntrl(skb_combo[i]))
			continue;
		*buf = skb_combo[i];
		buf++;
	}
	*buf = '\0';
	skb_ncombo = 0;

	insert(&ARG(.s = gsbuf));
}

void
render_line(struct line *l)
{
	if (*l->r != 0)
		return;
	for (size_t i = 0; i < VLINE_RENDER_MAX; i++) {
		if (i < l->l.len)
			l->r[i] = l->l.s[i];
		else
			l->r[i] = ' ';
	}
}

/* key functions */
void
insert(const union arg *arg)
{
	struct line *l = ctab->w->l;
	ctab->w->refresh = 1;
	if (!l) {
		l = ecalloc(1, sizeof(*l));
		utilsh_list_insert(&ctab->w->fb->lines,
				ctab->w->fb->lines.end, &l->link);
		ctab->w->l = l;
	}
	*l->r = 0;
	estr_insert_cstr(&l->l, ctab->w->ccol, arg->s);
}

void
mode(const union arg *arg)
{
	cmode = arg->i;
}

void
quit(const union arg *arg)
{
	running = 0;
}

/* command functions */
int
cmd_edit(int argc, const char *argv[])
{
	struct fbuf *fb;
	FILE *fp;
	struct line *l;

	if (argc > 1 && argv) {
		for (int i = 0; i < nfb; i++) {
			if (strcmp(fbs[nfb].path, argv[1]) == 0) {
				fb = &fbs[nfb];
				goto setwin;
			}
		}
	}

	nfb++;
	fbs = erealloc(fbs, sizeof(*fbs) * nfb);
	fb = &fbs[nfb - 1];

	utilsh_list_init(&fb->lines);

	if (argc <= 1 || !argv) {
		strcpy(fb->path, "<unnamed>");
		goto setwin;
	}

	strcpy(fb->path, argv[1]);

	if (!(fp = fopen(fb->path, "r")))
		return 0;

	while (fgets(gsbuf, BUFSIZ, fp)) {
		l = ecalloc(1, sizeof(*l));
		estr_from_cstr(&l->l, gsbuf);
		*l->r = 0;
		utilsh_list_insert(&fb->lines, fb->lines.end, &l->link);
	}

	fclose(fp);

setwin:
	ctab->w->crow = ctab->w->ccol = 0;
	ctab->w->fb = fb;
	ctab->w->l = utilsh_list_container_of(fb->lines.beg, struct line, link);
	ctab->w->refresh = 1;

	return 0;
}

// int
// cmd_quit(int argc, const char *argv[])
// {
// 	quit(NULL);
// 	return 0;
// }

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
		if (poll(gfds, 1, -1) == -1 && errno != EINTR)
			die("poll()");
		keypress();
	}
	fini();

	return 0;
}

#define SKB_IMPL
#include "skb.h"
