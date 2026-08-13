/* SPDX-License-Identifier: GPL-3.0-or-later */
static TextObj *textobj_get_ident(TextObj *t, int direction);

TextObj *
textobj_get_ident(TextObj *t, int direction)
{
	unsigned int beg, begset, pos;
	unsigned int lastbeg, lastend, lastset;

	size_t off = 0, ret;
	uint_least32_t cp;

	beg = begset = pos = 0;
	lastbeg = lastend = lastset = 0;
	grapheme_decode_iter(t->begln->s.s, ret, off, cp) {
		if (iswalnum((wchar_t)cp) || wcsrchr(L"_", cp)) {
			if (!begset) {
				beg = pos;
				begset = 1;
			}
		} else {
			if (direction < 0 && pos >= t->beg.col)
				break;
			if (begset) {
				if (pos > t->beg.col)
					break;
				lastbeg = beg;
				lastend = pos;
				lastset = 1;
			}
			begset = 0;
		}
		pos++;
	}

	if (direction < 0) {
		if (!lastset)
			return NULL;
		beg = lastbeg;
		pos = lastend;
		goto setobj;
	}

	if (!begset)
		return NULL;
setobj:
	t->ibeg.col = beg;
	t->iend.col = pos;
	t->abeg = t->ibeg;
	t->aend = t->iend;
	return t;
}

TextObj *
textobj_find_nex(int k, TextObj *t)
{
	uint_least32_t cp;
	Line *l = t->begln;
	size_t ret, off;

	t->ibeg = t->iend = t->abeg = t->aend = t->beg;

	off = coltobcol(l, t->beg.col);
	while (1) {
		grapheme_decode_iter(l->s.s, ret, off, cp) {
			if ((int)cp == k)
				goto end;
			t->aend.col++;
		}
		if (!findpassthrough || !l->link.nex)
			return NULL;
		off = 0;
		t->aend.row++;
		t->aend.col = 0;
		l = lineof(l->link.nex);
	}
end:
	t->iend = t->aend;
	t->aend.col++;
	return t;
}

TextObj *
textobj_find_prv(int k, TextObj *t)
{
	unsigned int col, found;
	uint_least32_t cp;
	Line *l = t->begln;
	size_t ret, off;

	t->ibeg = t->iend = t->abeg = t->aend = t->beg;

	while (1) {
		col = found = off = t->aend.col = 0;
		grapheme_decode_iter(l->s.s, ret, off, cp) {
			if (t->aend.col >= t->beg.col)
				break;
			if ((int)cp == k) {
				t->aend.col = col;
				found = 1;
			}
			col++;
		}
		if (found)
			break;
		if (!findpassthrough || !l->link.prv)
			return NULL;
		t->aend.row--;
		l = lineof(l->link.prv);
	}

	t->iend.row = t->aend.row;
	t->iend.col = t->aend.col + 1;

	return t;
}

TextObj *
textobj_get(TextObj *t, int direction)
{
	TextObj *r;
	t->ibeg = t->iend = t->abeg = t->aend = t->beg;
	if ((r = textobj_get_ident(t, direction)))
		return r;
	return NULL;
}
