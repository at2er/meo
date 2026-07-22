static TextObj *textobj_get_ident(TextObj *t);

TextObj *
textobj_get_ident(TextObj *t)
{
	unsigned int beg, begset, pos;

	size_t off = 0, ret;
	uint_least32_t cp;

	beg = begset = pos = 0;
	grapheme_decode_iter(t->begln->s.s, ret, off, cp) {
		if (iswalnum((wchar_t)cp) || wcsrchr(L"_-", cp)) {
			if (!begset) {
				beg = pos;
				begset = 1;
			}
		} else {
			if (pos > t->beg.col)
				break;
			begset = 0;
		}
		pos++;
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
	if ((r = textobj_get_ident(t)))
		return r;
	return NULL;
}
