typedef struct TextObj {
	Pos beg, end;
	Line *begln;
} TextObj;

static TextObj *textobj_get(TextObj *t, int direction);
