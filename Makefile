# mcmap/Makefile

CC = gcc

CFLAGS += -Wall -Werror -std=gnu99
CFLAGS += $(shell pkg-config --cflags gtk+-2.0 zlib)

LDFLAGS += $(shell pkg-config --libs gtk+-2.0 zlib)

sources = main.c protocol.c world.c

objs = $(sources:.c=.o)
deps = $(sources:.c=.d)

default: mcmap

-include $(deps)

.PHONY : default clean

mcmap: $(objs)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) mcmap $(objs) $(deps)

%.d: %.c
	@set -e; rm -f $@; \
	 $(CC) -MM $(CFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@
