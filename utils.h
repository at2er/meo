/* SPDX-License-Identifier: MIT */
#ifndef UTILS_H
#define UTILS_H
#include <stddef.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

#define xor_swap(A, B) \
	do { \
		(A) ^= (B); \
		(B) ^= (A); \
		(A) ^= (B); \
	} while (0)

int align(int num, int min, int max);
void die(const char *msg, ...);
void *ecalloc(size_t nmenb, size_t size);
void *erealloc(void *p, size_t s);

size_t _arealloc(void **p, size_t n, size_t o);
/* auto realloc when only n > o */
#define arealloc(P, N, O) _arealloc((void**)(P), N, O)

#endif
