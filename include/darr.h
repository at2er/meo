/* Public domain
 *
 * Just a dynamic array. */
#ifndef UTILSH_DARR_H
#define UTILSH_DARR_H
#include <stddef.h>
#include <string.h>

#ifndef UTILSH_DARR_REALLOC
#define UTILSH_DARR_REALLOC realloc
#include <stdlib.h>
#endif

#define darr(TYPE) \
	struct { \
		TYPE *e; \
		int n; \
	}

#define darr_append(DARR, ELEM) \
	do { \
		darr_expand(DARR); \
		darr_last(DARR) = (ELEM); \
	} while (0)

#define darr_expand(DARR) \
	darr_resize((DARR), (DARR)->n + 1);

#define darr_init(DARR) \
	do { \
		(DARR)->n = 0; \
		(DARR)->e = NULL; \
	} while (0)

#define darr_last(DARR) ((DARR)->e[(DARR)->n - 1])

#define darr_reduce(DARR) \
	do { \
		if ((DARR)->n <= 0) \
			break; \
		darr_resize((DARR), (DARR)->n - 1); \
	} while (0)

#define darr_remove(DARR, POS) \
	do { \
		if ((DARR)->n <= 0) \
			break; \
		memmove((DARR)->e + (POS), \
				(DARR)->e + (POS) + 1, \
				(DARR)->n - (POS)); \
		darr_resize((DARR), (DARR)->n - 1); \
	} while (0)

#define darr_resize(DARR, N) \
	do { \
		if ((DARR)->n == (N)) \
			break; \
		(DARR)->n = (N); \
		(DARR)->e = UTILSH_DARR_REALLOC((DARR)->e, \
				(DARR)->n * sizeof(*(DARR)->e)); \
	} while (0)

#endif
