-include debug.mk

CC = gcc
CFLAGS = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE -std=c99 -pedantic \
	 -Wall -Wextra -Wno-unused-parameter -Wno-implicit-fallthrough \
	 -Iinclude $(CFLAGS_DEBUG)
LDFLAGS = -L/usr/local/lib -lgrapheme
AR = ar
PREFIX = /usr/local
