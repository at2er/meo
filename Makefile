CC = gcc
CFLAGS = -D_DEFAULT_SOURCE -std=c99 -pedantic \
	 -Wall -Wextra -Wno-unused-parameter -Iinclude -g3 -ggdb
AR = ar
PREFIX = /usr/local

TARGET_DIR = $(PREFIX)/bin

TARGET = meo

SRC = meo.c utils.c $(wildcard include/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all clean install uninstall
all: $(TARGET)

%.o: %.c %.h config.h
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET): meo.h $(OBJ) $(wildcard include/*.h)
	$(CC) -o $@ $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)

install:
	mkdir -p $(TARGET_DIR)
	cp -f $(TARGET) $(TARGET_DIR)/$(TARGET)

uninstall:
	rm -f $(TARGET_DIR)/$(TARGET)
