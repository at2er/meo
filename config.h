static const int barattr = 0;
static const int nonprintattr = SCTUI_REVERSE;
static const int selattr = SCTUI_REVERSE;

static const char *tabrender = "        ";

static const char *modestr[] = {
	[ModeN] = "-",
	[ModeI] = "I",
	NULL
};

#define KESC 27

static const struct key keys_n[] = {
	{ { 'h' }, moveright, {.i = -1}     },
	{ { 'i' }, mode,      {.i = ModeI}  },
	{ { 'j' }, movedown,  {.i =  1}     },
	{ { 'k' }, movedown,  {.i = -1}     },
	{ { 'l' }, moveright, {.i =  1}     },
	{ { 'q' }, cmd,       {.s = "quit"} },
	{ {  0  }, NULL,      {0}           }
};

static const struct key keys_i[] = {
	{ { 'j', 'k' }, mode, {.i = ModeN}  },
	{ { 0        }, NULL, {0}           }
};

static const struct key *keys[] = {
	[ModeN] = keys_n,
	[ModeI] = keys_i,
	NULL
};

static const Cmd cmds[] = {
	{ "e",    cmdedit },
	{ "edit", cmdedit },
	{ "q",    cmdquit },
	{ "quit", cmdquit },
	{ NULL,   NULL    },
};
