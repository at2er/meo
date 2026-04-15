#include "list.h"
#include "str.h"

#define VLINE_RENDER_MAX 2048

enum { MODE_NOR, MODE_INS, MODE_CMD, MODE_SEARCH };
enum { GOTO_IN_FILE, GOTO_IN_LINE };
enum { SPLIT_HOR, SPLIT_VER };
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

struct cursor {
	int row, col, sel;
	struct line *l;
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

	unsigned int ldirty:1, /* lines dirty */
	             tmp:1;    /* auto remove after quit */

	struct marker pos;
};
typedef darr(struct fbuf *) fb_arr;

struct line {
	struct str s;
	char r[VLINE_RENDER_MAX];

	struct utilsh_list link;
};

typedef darr(struct tab  *) tab_arr;
typedef darr(struct win  *) win_arr;
struct tab {
	struct win *w;
	win_arr wins;
};

struct win {
	struct line *draw;
	struct marker p;
	struct win *prv;
	unsigned int refresh:1,
	             split:1; /* 0:hor 1:ver */
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
static void search(const union arg *arg);
static void sel(const union arg *arg);
static void sel_line(const union arg *arg);
static void sel_word(const union arg *arg);
static void split_win(const union arg *arg);
static void yank(const union arg *arg);

/* command functions */
static void cmd_buffer(int argc, const char *argv[]);
static void cmd_edit(int argc, const char *argv[]);
static void cmd_marks(int argc, const char *argv[]);
static void cmd_write(int argc, const char *argv[]);
static void cmd_quit(int argc, const char *argv[]);
