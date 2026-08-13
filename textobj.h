/* SPDX-License-Identifier: GPL-3.0-or-later */
typedef struct TextObj {
	Pos beg;
	Line *begln;
	Pos ibeg, iend, abeg, aend;
} TextObj;

static TextObj *textobj_find_nex(int k, TextObj *t);
static TextObj *textobj_find_prv(int k, TextObj *t);
static TextObj *textobj_get(TextObj *t, int direction);
