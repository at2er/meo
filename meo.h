#include "list.h"
#include "str.h"

#define VLINE_RENDER_MAX 2048

enum { MODE_NOR, MODE_INS, MODE_CMD, MODE_SEARCH };
enum { GOTO_IN_FILE, GOTO_IN_LINE };
enum { UP, DOWN };

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

struct marker {
	struct fbuf *fb;
	struct line *l;
	int row, rowoff, col;
};

struct fbuf {
	struct utilsh_list_head lines;
	int nline;
	char path[FILENAME_MAX];

	struct marker pos;
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
static void backspace(const union arg *arg);
static void change(const union arg *arg);
static void cmd(const union arg *arg);
static void concat_line(const union arg *arg);
static void delete(const union arg *arg);
static void goto_beg(const union arg *arg);
static void goto_end(const union arg *arg);
static void goto_mark(const union arg *arg);
static void insert(const union arg *arg);
static void mark(const union arg *arg);
static void mode(const union arg *arg);
static void move_col(const union arg *arg);
static void move_row(const union arg *arg);
static void new_line(const union arg *arg);
static void paste(const union arg *arg);
static void quit(const union arg *arg);
static void search(const union arg *arg);
static void sel(const union arg *arg);
static void sel_line(const union arg *arg);
static void sel_word(const union arg *arg);
static void yank(const union arg *arg);

/* command functions */
static void cmd_edit(int argc, const char *argv[]);
static void cmd_write(int argc, const char *argv[]);
static void cmd_quit(int argc, const char *argv[]);
