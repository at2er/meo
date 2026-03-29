CC = gcc
CFLAGS = -D_DEFAULT_SOURCE -std=c99 -pedantic \
	 -Wall -Wextra -Wno-unused-parameter -Iinclude -g3
AR = ar
PREFIX = /usr/local

TARGET_DIR = $(PREFIX)/bin

TARGET = sved

SRC = sved.c utils.c $(wildcard include/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all clean install uninstall
all: $(TARGET)

%.o: %.c config.h sved.h $(wildcard include/*.h)
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET): $(OBJ)
	$(CC) -o $@ $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)

install:
	mkdir -p $(TARGET_DIR)
	cp -f $(TARGET) $(TARGET_DIR)/$(TARGET)

uninstall:
	rm -f $(TARGET_DIR)/$(TARGET)
