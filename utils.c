/* SPDX-License-Identifier: MIT */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grapheme.h"
#include "sctui.h"
#include "utils.h"

unsigned int
align(unsigned int num, unsigned int min, unsigned int max)
{
	if (num > max)
		num = max;
	if (num < min)
		num = min;
	return num;
}

void
die(const char *msg, ...)
{
	va_list ap;

	if (global_sctui.init)
		sctui_fini();

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);

	exit(1);
}

void *
ecalloc(size_t nmenb, size_t size)
{
	void *p = calloc(nmenb, size);
	if (!p)
		die("failed to calloc\n");
	return p;
}

void *
erealloc(void *p, size_t s)
{
	if (!p)
		p = ecalloc(1, s);
	else
		p = realloc(p, s);
	if (!p)
		die("realloc()");
	return p;
}

char *
toprint(char buf[2], char c)
{
	if (isprint(c)) {
		buf[0] = c;
		buf[1] = '\0';
	} else {
		buf[0] = '^';
		if (c == 127)
			buf[1] = '?';
		else
			buf[1] = c + 0x40;
	}
	return buf;
}

int
unsigned ustrlen(const char *s)
{
	unsigned int ulen = 0;
	for (int i = 0; s[i]; ulen++)
		i += grapheme_next_character_break_utf8(s+i, SIZE_MAX);
	return ulen;
}

int
utf8_blen(unsigned char first_byte)
{
	if (first_byte <= 0x7F)
		return 1;
	else if (RANGE(first_byte, 0xC0, 0xDF))
		return 2;
	else if (RANGE(first_byte, 0xE0, 0xEF))
		return 3;
	else if (RANGE(first_byte, 0xF0, 0xF7))
		return 4;
	return -1;
}
