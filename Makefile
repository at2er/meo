include config.mk

SRC = meo.c utils.c $(wildcard include/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all clean install uninstall
all: meo

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@
meo.o: textobj.c textobj.h clipboard.h config.h


meo: $(OBJ) $(wildcard include/*.h)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f meo $(DESTDIR)$(PREFIX)/bin/meo

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/meo
