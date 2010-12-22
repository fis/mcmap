ifeq ($(origin objdir), undefined)
	ifeq ($(origin OBJDIR), undefined)
		objdir := $(if $(debug),_debug,_build)
	endif
endif

include useful.make

libs := gio-2.0 gthread-2.0 sdl

cc.flags := -Wall -Werror -std=gnu99
cc.flags += $(shell pkg-config --cflags $(libs))
ld.flags += $(shell pkg-config --libs $(libs)) -lz -lreadline

cc.flags += $(if $(debug),-g,-O3)

all: $(objdir)/mcmap

$(call c-program, mcmap, cmd.c console.c main.c map.c protocol.c world.c)
