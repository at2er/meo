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
		t->beg.col = lastbeg;
		t->end.col = lastend;
		return t;
	}

	if (!begset)
		return NULL;
	t->beg.col = beg;
	t->end.col = pos;
	return t;
}

TextObj *
textobj_get(TextObj *t, int direction)
{
	TextObj *r;
	t->end = t->beg;
	if ((r = textobj_get_ident(t, direction)))
		return r;
	return NULL;
}
