/* SPDX-License-Identifier: MIT */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "sctui.h"

int
align(int num, int min, int max)
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

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);

	if (global_sctui.init)
		sctui_fini();
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

size_t
_arealloc(void **p, size_t n, size_t o)
{
	if (!(*p)) {
		*p = calloc(1, n);
		return n;
	}
	if (o >= n)
		return o;
	*p = erealloc(*p, n);
	return n;
}
