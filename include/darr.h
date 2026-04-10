/* Public domain
 *
 * Just a dynamic array. */
#ifndef UTILSH_DARR_H
#define UTILSH_DARR_H
#include <stddef.h>

#ifndef UTILSH_DARR_REALLOC
#define UTILSH_DARR_REALLOC realloc
#include <stdlib.h>
#endif

#define darr(TYPE) \
	struct { \
		TYPE *elems; \
		int n; \
	}

#define darr_append(DARR, ELEM) \
	do { \
		darr_expand(DARR); \
		(DARR)->elems[(DARR)->n - 1] = (ELEM); \
	} while (0)

#define darr_expand(DARR) \
	darr_resize((DARR), (DARR)->n + 1);

#define darr_init(DARR) \
	do { \
		(DARR)->n = 0; \
		(DARR)->elems = NULL; \
	} while (0)

#define darr_resize(DARR, N) \
	do { \
		(DARR)->n = (N); \
		(DARR)->elems = UTILSH_DARR_REALLOC((DARR)->elems, \
				(DARR)->n * sizeof(*(DARR)->elems)); \
	} while (0)

#endif
