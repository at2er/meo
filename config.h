static const int sel_attr = SCTUI_BGSET(SCTUI_BLACK);

static const char *tab_render = "        ";

static const char *mode_str[] = {
	[MODE_NOR]    = NULL,
	[MODE_INS]    = "-- INS --",
	[MODE_CMD]    = NULL,
	NULL
};

static const struct key normal_keys[] = {
	/* key   func         arg           */
	{"gg",   goto_beg,    {0}            },
	{"gG",   goto_end,    {0}            },
	{"G",    goto_end,    {0}            },
	{"h",    move_col,    {.i = -1}      },
	{"j",    move_row,    {.i =  1}      },
	{"k",    move_row,    {.i = -1}      },
	{"l",    move_col,    {.i =  1}      },
	{"i",    mode,        {.i = MODE_INS}},
	{"m",    mark,        {0}            },
	{"o",    new_line,    {.i = 1}       },
	{"O",    new_line,    {.i = 0}       },
	{"q",    quit,        {0}            },
	{"w",    sel_word,    {0}            },
	{"'",    goto_mark,   {0}            },
	{":",    mode,        {.i = MODE_CMD}},
	{"^d",   move_row,    {.i =  10}     },
	{"^u",   move_row,    {.i = -10}     },
	{NULL,   NULL,        {0}            }
};

static const struct key insert_keys[] = {
	/* key   func         arg           */
	{"jk",   mode,        {.i = MODE_NOR}},
	{"/b",   delete,      {.i = 1}       },
	{"/e",   mode,        {.i = MODE_NOR}},
	{"/r",   insert,      {.s = "\n"}    },
	{NULL,   NULL,        {0}            }
};

static const struct key cmd_keys[] = {
	/* key   func         arg           */
	{"/b",   delete,      {.i = 1}       },
	{"/c",   mode,        {.i = MODE_NOR}},
	{"/e",   mode,        {.i = MODE_NOR}},
	{"/r",   cmd,         {0}            },
	{NULL,   NULL,        {0}            }
};

static struct cmd cmds[] = {
	/* cmd     alias   func    */
	{"edit",   "e",    cmd_edit },
	{"write",  "w",    cmd_write},
	{"quit",   "q",    cmd_quit },
	{NULL,     NULL,   NULL     }
};
