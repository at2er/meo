/** Simple Key binding
 * A simple header only library to handle key press with `sctui`
 * in terminal user interface program.
 *
 * Put 'SKB_IMPL' to one source file to compile it and use it.
 *
 * @important: `get_keys_table` must be declared before include this file.
 * `const struct key *get_keys_table(void)`
 *
 * Keys:
 *   - Control (ctrl):
 *     Starting with '^' means 'ctrl + X', such as "^c" means 'ctrl + c'.
 *     ( see **Special Characters** )
 *
 *   - Shift:
 *     * Visible ascii characters: shift changes their certain key.
 *     ( see **Special Characters** )
 *
 *   - Special Characters:
 *     * Backspace:    "/b"
 *     * Enter/Return: "/r"
 *     * '^': "^^"
 *     * '/': "//"
 *
 * Option macros: #bool(defined: true, undefined: false)
 *   SKB_MAX_KEYCOMBO -> int
 *
 *   SKB_IMPL -> bool:
 *     Put implment to a file to use this library.
 *
 *   SKB_REDEFINE_ARG, SKB_REDEFINE_KEY -> member of struct 'union arg':
 *     Just like define a struct:
 *         #define SKB_REDEFINE_ARG \
 *                 struct window *win; \
 *                 <type> <ident> ...
 *
 * MIT License
 *
 * Copyright (c) 2025 at2er <xb0515@outlook.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef SKB_H
#define SKB_H
#include <stdbool.h>
#include "sctui.h"

#ifndef SKB_MAX_KEYCOMBO
#define SKB_MAX_KEYCOMBO 5
#endif

#ifndef SKB_REDEFINE_ARG
#define SKB_REDEFINE_ARG
#endif

#ifndef SKB_REDEFINE_KEY
#define SKB_REDEFINE_KEY
#endif

enum _SKB_APPLY_KEY_RESULT {
	_SKB_APPLY_KEY_NOT_FOUND,
	_SKB_APPLY_KEY_SUCCESS,
	_SKB_APPLY_KEY_USABLE_KEY
};

union arg {
	int i;
	const char *s;
	unsigned int ui;
	void *v;
	SKB_REDEFINE_ARG
};

struct key {
	const char *keys;
	void (*func)(const union arg *arg);
	const union arg arg;
	SKB_REDEFINE_KEY
};

extern bool skb_handle_key(int key);

extern int skb_combo[SKB_MAX_KEYCOMBO];
extern int skb_ncombo;

#endif /* SKB_H */

#ifdef SKB_IMPL
#include <ctype.h>
#include <string.h>

int skb_combo[SKB_MAX_KEYCOMBO];
int skb_ncombo;

static int
_skb_compare_key(const char *key, int pressed)
{
#define SPECIAL(K, P, C) \
	if (pressed == (K)) { \
		if (key[0] == (P) && key[1] == (C)) \
			return 2; \
		return 0; \
	}
	SPECIAL(127, '/', 'b') else
	SPECIAL(13,  '/', 'r') else
	SPECIAL(27,  '/', 'e') else
	SPECIAL('/', '/', '/') else
	SPECIAL('^', '^', '^') else
	if (iscntrl(pressed)) {
		if (key[0] != '^')
			return 0;
		if (TK_CTRL(key[1]) == TK_CTRL(pressed))
			return 2;
	}
#undef SPECIAL
	if (pressed != key[0])
		return 0;
	return 1;
}

static enum _SKB_APPLY_KEY_RESULT
_skb_apply_key(const struct key *key)
{
	int r;
	for (int i = 0, ki = 0; i < skb_ncombo; i++) {
		r = _skb_compare_key(&key->keys[ki], skb_combo[i]);
		if (r < 1)
			break;
		ki += r;
		if (i != skb_ncombo - 1)
			continue;
		if (key->keys[ki] != '\0')
			return _SKB_APPLY_KEY_USABLE_KEY;
		key->func(&key->arg);
		skb_ncombo = 0;
		return _SKB_APPLY_KEY_SUCCESS;
	}
	return _SKB_APPLY_KEY_NOT_FOUND;
}

bool
skb_handle_key(int key)
{
	const struct key *keys;
	enum _SKB_APPLY_KEY_RESULT ret;
	bool usable = false;
	skb_combo[skb_ncombo++] = key;
	keys = get_keys_table();
	for (int i = 0; keys[i].keys != NULL; i++) {
		ret = _skb_apply_key(&keys[i]);
		if (ret == _SKB_APPLY_KEY_SUCCESS)
			return true;
		if (ret == _SKB_APPLY_KEY_USABLE_KEY)
			usable = true;
	}
	return usable;
}
#endif /* SKB_IMPL */
