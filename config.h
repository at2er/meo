#ifndef CONFIG_H
#define CONFIG_H

static const struct key normal_keys[] = {
	/* key   func       arg      */
	{"i",    mode,      {.i = 'i'}},
	{"q",    quit,      {0}       },
	{NULL,   NULL,      {0}       }
};

static const struct key insert_keys[] = {
	/* key   func       arg      */
	{"jk",   mode,      {.i = 'n'}},
	{NULL,   NULL,      {0}       }
};

// static struct cmd cmds[] = {
// 	/* cmd      alias   func     */
// 	{"edit",    "e",    cmd_edit  },
// 	{"quit",    "q",    cmd_quit  },
// 	{NULL,      NULL,   NULL      }
// };

#endif
