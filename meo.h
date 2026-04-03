#include "list.h"
#include "str.h"

#define VLINE_RENDER_MAX 2048

struct fbuf;
struct win;

#define SKB_REDEFINE_ARG \
	struct win *win;
#include "skb.h"

struct cmd {
	const char *cmd;   /* full command,  like 'write' */
	const char *alias; /* command alias, like 'w'     */
	void (*func)(int argc, const char *argv[]);
};

struct fbuf {
	struct utilsh_list_head lines;
	int nline;
	char path[FILENAME_MAX];

	int row, rowoff, col;
};

struct line {
	struct str s;
	char r[VLINE_RENDER_MAX];

	struct utilsh_list link;
};

struct tab {
	struct win *w;
	struct win **wins;
	int nwins;
};

struct win {
	struct fbuf *fb;
	struct line *l, *draw;
	int refresh;
	int row, rowoff, col;
	int x, y, w, h;
};

/* key functions */
static void cmd(const union arg *arg);
static void delete(const union arg *arg);
static void insert(const union arg *arg);
static void mode(const union arg *arg);
static void move_col(const union arg *arg);
static void move_row(const union arg *arg);
static void new_line(const union arg *arg);
static void quit(const union arg *arg);

/* command functions */
static void cmd_edit(int argc, const char *argv[]);
static void cmd_write(int argc, const char *argv[]);
static void cmd_quit(int argc, const char *argv[]);
