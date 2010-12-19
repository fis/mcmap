# mcmap/Makefile

CC := gcc

LIBS := gio-2.0 sdl zlib

CFLAGS := $(CFLAGS)
CFLAGS += -Wall -Werror -std=gnu99
CFLAGS += $(shell pkg-config --cflags $(LIBS))
CFLAGS += -g

LDFLAGS := $(LDFLAGS)
LDFLAGS += $(shell pkg-config --libs $(LIBS)) -lreadline

sources := cmd.c main.c map.c protocol.c world.c

objs := $(sources:.c=.o)
deps := $(sources:.c=.d)

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
