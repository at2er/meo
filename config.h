static int barattr = 0;
static unsigned int findpassthrough = 1;
static int nonprintattr = SCTUI_REVERSE;
static int selattr = SCTUI_REVERSE;

static const char *tabrender = "        ";

static const char *modestr[] = {
	[ModeN] = "-",
	[ModeC] = "C",
	[ModeF] = "F",
	[ModeI] = "I",
	[ModeV] = "V",
	NULL
};

#define KBS  127
#define KCR  13
#define KESC 27
/* Maybe you need to use KCR */
#define KLF  10
#define KSP  32

static const struct key keys_n[] = {
	{ { 'b'       }, selword,        {.i = -1}     },
	{ { 'c'       }, change,         {.i = '+'}    },
	{ { 'd'       }, delete,         {.i = '+'}    },
	{ { 'f'       }, findnex,        {0}           },
	{ { 'F'       }, findprv,        {0}           },
	{ { 'g', 'b'  }, gotoinline,     {.i = -1}     },
	{ { 'g', 'f'  }, gotoinline,     {.i =  1}     },
	{ { 'g', 'g'  }, gotoinfile,     {.i = -1}     },
	{ { 'g', 'l'  }, selline,        {0}           },
	{ { 'G'       }, gotoinfile,     {.i =  1}     },
	{ { 'h'       }, moveright,      {.i = -1}     },
	{ { 'i'       }, mode,           {.i = ModeI}  },
	{ { 'j'       }, movedown,       {.i =  1}     },
	{ { 'k'       }, movedown,       {.i = -1}     },
	{ { 'l'       }, moveright,      {.i =  1}     },
	{ { 'm'       }, mark,           {0}           },
	{ { 'n'       }, nexmatch,       {.i =  1}     },
	{ { 'N'       }, nexmatch,       {.i = -1}     },
	{ { 'o'       }, newline,        {.i =  1}     },
	{ { 'O'       }, newline,        {.i = -1}     },
	{ { 'p'       }, paste,          {.i = '+'}    },
	{ { 'q'       }, record,         {0}           },
	{ { 'Q'       }, cmd,            {.s = "quit"} },
	{ { 'r'       }, redo,           {0}           },
	{ { 't'       }, tillnex,        {0}           },
	{ { 'T'       }, tillprv,        {0}           },
	{ { 'u'       }, undo,           {0}           },
	{ { 'v'       }, mode,           {.i = ModeV}  },
	{ { 'w'       }, selword,        {.i =  1}     },
	{ { 'x'       }, swapsel,        {0}           },
	{ { 'y'       }, yank,           {.i = '+'}    },
	{ { '$'       }, gotoinline,     {.i =  1}     },
	{ { '/'       }, mode,           {.i = ModeF}  },
	{ { ':'       }, mode,           {.i = ModeC}  },
	{ { '@'       }, execreg,        {0}           },
	{ { '\''      }, gotomark,       {0}           },
	{ { '0'       }, gotoinline,     {.i = -1}     },
	{ { CTRL('c') }, cmd,            {.s = "quit"} },
	{ { CTRL('u') }, movedown,       {.i = -10}    },
	{ { CTRL('d') }, movedown,       {.i =  10}    },
	{ { CTRL('z') }, suspend,        {0}           },
	{ { KSP, 'b'  }, cmd,            {.s = "b #"}  },
	{ {0},           NULL,           {0}           }
};

static const struct key keys_c[] = {
	{ { CTRL('b') }, moveright,      {.i = -1}     },
	{ { CTRL('c') }, mode,           {.i = ModeN}  },
	{ { CTRL('f') }, moveright,      {.i =  1}     },
	{ { CTRL('h') }, backspace,      {0}           },
	{ { CTRL('r') }, paste,          {0}           },
	{ { KBS       }, backspace,      {0}           },
	{ { KESC      }, mode,           {.i = ModeN}  },
	{ { KLF       }, cmd,            {0}           },
	{ {0},           NULL,           {0}           }
};

static const struct key keys_f[] = {
	{ { CTRL('b') }, moveright,      {.i = -1}     },
	{ { CTRL('c') }, mode,           {.i = ModeN}  },
	{ { CTRL('f') }, moveright,      {.i =  1}     },
	{ { CTRL('h') }, backspace,      {0}           },
	{ { CTRL('r') }, paste,          {0}           },
	{ { KBS       }, backspace,      {0}           },
	{ { KLF       }, search,         {0}           },
	{ { KESC      }, mode,           {.i = ModeN}  },
	{ {0},           NULL,           {0}           }
};

static const struct key keys_i[] = {
	{ { 'j', 'k'  }, mode,           {.i = ModeN}  },
	{ { CTRL('b') }, moveright,      {.i = -1}     },
	{ { CTRL('c') }, mode,           {.i = ModeN}  },
	{ { CTRL('f') }, moveright,      {.i =  1}     },
	{ { CTRL('h') }, backspace,      {0}           },
	{ { CTRL('n') }, movedown,       {.i =  1}     },
	{ { CTRL('p') }, movedown,       {.i = -1}     },
	{ { CTRL('r') }, paste,          {0}           },
	{ { KBS       }, backspace,      {0}           },
	{ { KESC      }, mode,           {.i = ModeN}  },
	{ {0},           NULL,           {0}           }
};

static const struct key keys_v[] = {
	{ { 'c'       }, change,         {.i = '+'}    },
	{ { 'd'       }, delete,         {.i = '+'}    },
	{ { 'f'       }, findnex,        {0}           },
	{ { 'F'       }, findprv,        {0}           },
	{ { 'h'       }, moveright,      {.i = -1}     },
	{ { 'j'       }, movedown,       {.i =  1}     },
	{ { 'k'       }, movedown,       {.i = -1}     },
	{ { 'l'       }, moveright,      {.i =  1}     },
	{ { 'p'       }, paste,          {.i = '+'}    },
	{ { 't'       }, tillnex,        {0}           },
	{ { 'T'       }, tillprv,        {0}           },
	{ { 'x'       }, swapsel,        {0}           },
	{ { 'y'       }, yank,           {.i = '+'}    },
	{ { CTRL('c') }, mode,           {.i = ModeN}  },
	{ { KESC      }, mode,           {.i = ModeN}  },
	{ {0},           NULL,           {0}           }
};

static const struct key *keys[] = {
	[ModeN] = keys_n,
	[ModeC] = keys_c,
	[ModeF] = keys_f,
	[ModeI] = keys_i,
	[ModeV] = keys_v,
	NULL
};

static const Cmd cmds[] = {
	{ "b",     cmdbuffer },
	{ "buf",   cmdbuffer },
	{ "e",     cmdedit   },
	{ "edit",  cmdedit   },
	{ "q",     cmdquit   },
	{ "quit",  cmdquit   },
	{ "w",     cmdwrite  },
	{ "write", cmdwrite  },
	{ NULL,    NULL      }
};
