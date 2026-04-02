static const struct key normal_keys[] = {
	/* key   func         arg       */
	{"h",    move_col,    {.i = -1}  },
	{"j",    move_row,    {.i =  1}  },
	{"k",    move_row,    {.i = -1}  },
	{"l",    move_col,    {.i =  1}  },
	{"i",    mode,        {.i = 'i'} },
	{"q",    quit,        {0}        },
	{":",    mode,        {.i = 'c'} },
	{NULL,   NULL,        {0}        }
};

static const struct key insert_keys[] = {
	/* key   func         arg       */
	{"jk",   mode,        {.i = 'n'} },
	{"/b",   delete,      {.i = 1}   },
	{"/e",   mode,        {.i = 'n'} },
	{"/r",   insert,      {.s = "\n"}},
	{NULL,   NULL,        {0}        }
};

static const struct key cmd_keys[] = {
	/* key   func         arg       */
	{"/b",   delete,      {.i = 1}   },
	{"/c",   mode,        {.i = 'n'} },
	{"/e",   mode,        {.i = 'n'} },
	{"/r",   cmd,         {.s = "e"}},
	{NULL,   NULL,        {0}        }
};

static struct cmd cmds[] = {
	/* cmd      alias   func     */
	{"edit",    "e",    cmd_edit  },
	{"quit",    "q",    cmd_quit  },
	{NULL,      NULL,   NULL      }
};
