/** Simple Key binding
 * A simple header only library to handle key press with `sctui`
 * in terminal user interface program.
 *
 * Put 'SKB_IMPL' to one source file to compile it and use it.
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

union arg {
	int i;
	const char *s;
	unsigned int ui;
	void *v;
	SKB_REDEFINE_ARG
};

struct key {
	int keys[SKB_MAX_KEYCOMBO];
	void (*func)(const union arg *arg);
	const union arg arg;
	SKB_REDEFINE_KEY
};

extern int skb_dropcombo(void);

/* Get the number of matched keys */
extern const struct key *skb_match(const struct key *keys);

/* @return: -1: key combo not found (won't reset skb_ncombo)
             0: key combo found but full matched
             1: key combo found and applied */
extern int skb_keypress(int key, const struct key *keys);

extern int skb_combo[SKB_MAX_KEYCOMBO];
extern int skb_ncombo;

#endif /* SKB_H */

#ifdef SKB_IMPL
#include <ctype.h>
#include <string.h>

int skb_combo[SKB_MAX_KEYCOMBO];
int skb_ncombo;

int
skb_dropcombo(void)
{
	int n = skb_ncombo;
	skb_ncombo = 0;
	return n;
}

const struct key *
skb_match(const struct key *keys)
{
	const struct key *k;
	for (k = keys; k->keys[0]; k++) {
		for (int i = 0; i < skb_ncombo; i++) {
			if (k->keys[i] != skb_combo[i])
				break;
			if (i == skb_ncombo - 1)
				return k;
		}
	}
	return NULL;
}

int
skb_keypress(int key, const struct key *keys)
{
	const struct key *k;
	skb_combo[skb_ncombo] = key;
	skb_ncombo++;
	if (!(k = skb_match(keys)))
		return -1;
	if (skb_ncombo == SKB_MAX_KEYCOMBO || k->keys[skb_ncombo] == 0) {
		k->func(&k->arg);
		skb_ncombo = 0;
		return 1;
	}
	return 0;
}
#endif /* SKB_IMPL */
