static const char **
sys_copy_cmd()
{
	static const char *c[] = {"wl-copy", NULL};
	if (!getenv("WAYLAND_DISPLAY"))
		return NULL;
	return c;
}

static const char **
sys_paste_cmd()
{
	static const char *c[] = {"wl-paste", "-n", NULL};
	if (!getenv("WAYLAND_DISPLAY"))
		return NULL;
	return c;
}

static const int sel_attr = SCTUI_REVERSE;
static const int bar_attr = 0;

static const char *tab_render = "        ";

static const char *mode_str[] = {
	[MODE_NOR]    = NULL,
	[MODE_INS]    = "-- INS --",
	[MODE_CMD]    = NULL,
	[MODE_SEARCH] = NULL,
	NULL
};

static const struct key normal_keys[] = {
	/* key   func         arg               */
	{"b",    sel_word,    {.i = -1}          },
	{"c",    change,      {0}                },
	{"d",    delete,      {0}                },
	{"f",    find_nex,    {.i = 1}           },
	{"F",    find_prv,    {.i = 1}           },
	{"gg",   goto_beg,    {.i = GOTO_IN_FILE}},
	{"gG",   goto_end,    {.i = GOTO_IN_FILE}},
	{"gb",   sel_line,    {.i = -1}          },
	{"ge",   sel_line,    {.i =  1}          },
	{"gh",   goto_beg,    {.i = GOTO_IN_LINE}},
	{"gl",   goto_end,    {.i = GOTO_IN_LINE}},
	{"G",    goto_end,    {.i = GOTO_IN_FILE}},
	{"h",    move_col,    {.i = -1}          },
	{"j",    move_row,    {.i =  1}          },
	{"k",    move_row,    {.i = -1}          },
	{"l",    move_col,    {.i =  1}          },
	{"i",    mode,        {.i = MODE_INS}    },
	{"m",    mark,        {0}                },
	{"n",    search,      {.i =  1}          },
	{"N",    search,      {.i = -1}          },
	{"o",    new_line,    {.i =  1}          },
	{"O",    new_line,    {.i = -1}          },
	{"p",    paste,       {.s = "+"   }      },
	{"P",    paste,       {.s = "+P"  }      },
	{"u",    undo,        {0}                },
	{"r",    redo,        {0}                },
	{"v",    sel,         {0}                },
	{"w",    sel_word,    {.i = 1}           },
	{"x",    swap_sel,    {0}                },
	{"y",    yank,        {.i = '+'}         },
	{"'",    goto_mark,   {0}                },
	{"//",   mode,        {.i = MODE_SEARCH} },
	{":",    mode,        {.i = MODE_CMD}    },
	{"^d",   move_row,    {.i =  10}         },
	{"^l",   redraw,      {0}                },
	{"^u",   move_row,    {.i = -10}         },
	{"^z",   suspend,     {0}                },
	{"/sww", focus_win,   {.i = FOCUS_PRV}   },
	{"/swm", focus_win,   {.i = FOCUS_MAIN}  },
	{"/swt", focus_win,   {.i = FOCUS_TMP}   },
	{"/sq",  cmd,         {.s = "quit"}      },
	{NULL,   NULL,        {0}                }
};

static const struct key insert_keys[] = {
	/* key   func         arg               */
	{"jk",   mode,        {.i = MODE_NOR}    },
	{"/b",   backspace,   {.i = 1}           },
	{"/e",   mode,        {.i = MODE_NOR}    },
	{"/r",   insert,      {.s = "\n"}        },
	{"^h",   backspace,   {.i = 1}           },
	{NULL,   NULL,        {0}                }
};

/* search mode also use this bindings */
static const struct key cmd_keys[] = {
	/* key   func         arg               */
	{"/b",   backspace,   {.i = 1}           },
	{"/c",   mode,        {.i = MODE_NOR}    },
	{"/e",   mode,        {.i = MODE_NOR}    },
	{"/r",   cmd,         {0}                },
	{"^h",   backspace,   {.i = 1}           },
	{NULL,   NULL,        {0}                }
};

static struct cmd cmds[] = {
	/* cmd       alias   func      */
	{"buffer",   "b",    cmd_buffer },
	{"edit",     "e",    cmd_edit   },
	{"marks",    "ms",   cmd_marks  },
	{"write",    "w",    cmd_write  },
	{"quit",     "q",    cmd_quit   },
	{"shell",    "sh",   cmd_shell  },
	{NULL,       NULL,   NULL       }
};
