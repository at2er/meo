/* SPDX-License-Identifier: MIT */
#ifndef UTILS_H
#define UTILS_H
#include <stddef.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define LENGTH(ARR) (sizeof(ARR) / sizeof((ARR)[0]))
#define RANGE(X, MIN, MAX) ((X) >= (MIN) && (X) <= (MAX))
#define strmatch(STR, IDXT, IDX, N, ELEM) \
	for (IDXT IDX = 0; IDX < (N); IDX++) \
		if (strcmp((ELEM), (STR)) == 0)

unsigned int align(unsigned int num, unsigned int min, unsigned int max);
void die(const char *msg, ...);
void *ecalloc(size_t nmenb, size_t size);
void *erealloc(void *p, size_t s);
char *toprint(char buf[2], char c);
unsigned int ustrlen(const char *str);
int utf8_blen(unsigned char first_byte);

size_t _arealloc(void **p, size_t n, size_t o);
/* auto realloc when only n > o */
#define arealloc(P, N, O) _arealloc((void**)(P), N, O)

#endif
