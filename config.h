static const char **
copy_cmd()
{
	static const char *c[] = {"wl-copy", NULL};
	if (!getenv("WAYLAND_DISPLAY"))
		return NULL;
	return c;
}

static const int sel_attr = SCTUI_BGSET(SCTUI_BLACK);

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
	{"gg",   goto_beg,    {.i = GOTO_IN_FILE}},
	{"gG",   goto_end,    {.i = GOTO_IN_FILE}},
	{"gb",   goto_beg,    {.i = GOTO_IN_LINE}},
	{"ge",   goto_end,    {.i = GOTO_IN_LINE}},
	{"gh",   sel_line,    {.i = -1}          },
	{"gl",   sel_line,    {.i =  1}          },
	{"G",    goto_end,    {.i = GOTO_IN_FILE}},
	{"h",    move_col,    {.i = -1}          },
	{"j",    move_row,    {.i =  1}          },
	{"k",    move_row,    {.i = -1}          },
	{"l",    move_col,    {.i =  1}          },
	{"i",    mode,        {.i = MODE_INS}    },
	{"m",    mark,        {0}                },
	{"n",    search,      {.i =  1}          },
	{"N",    search,      {.i = -1}          },
	{"o",    new_line,    {.i = DOWN}        },
	{"O",    new_line,    {.i = UP}          },
	{"p",    paste,       {.i = '+'}         },
	{"q",    quit,        {0}                },
	{"w",    sel_word,    {.i = 1}           },
	{"y",    yank,        {.i = '+'}         },
	{"'",    goto_mark,   {0}                },
	{"//",   mode,        {.i = MODE_SEARCH} },
	{":",    mode,        {.i = MODE_CMD}    },
	{"^d",   move_row,    {.i =  10}         },
	{"^u",   move_row,    {.i = -10}         },
	{NULL,   NULL,        {0}                }
};

static const struct key insert_keys[] = {
	/* key   func         arg               */
	{"jk",   mode,        {.i = MODE_NOR}    },
	{"/b",   delete,      {.i = 1}           },
	{"/e",   mode,        {.i = MODE_NOR}    },
	{"/r",   insert,      {.s = "\n"}        },
	{"^h",   delete,      {.i = 1}           },
	{NULL,   NULL,        {0}                }
};

/* search mode also use this bindings */
static const struct key cmd_keys[] = {
	/* key   func         arg               */
	{"/b",   delete,      {.i = 1}           },
	{"/c",   mode,        {.i = MODE_NOR}    },
	{"/e",   mode,        {.i = MODE_NOR}    },
	{"/r",   cmd,         {0}                },
	{NULL,   NULL,        {0}                }
};

static struct cmd cmds[] = {
	/* cmd     alias   func    */
	{"edit",   "e",    cmd_edit },
	{"write",  "w",    cmd_write},
	{"quit",   "q",    cmd_quit },
	{NULL,     NULL,   NULL     }
};
