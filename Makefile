CC           ?= clang
CFLAGS       := -std=c99 -pedantic -Wall -Wextra -Wunused -Wswitch-enum
INCLUDES     := $(shell pkg-config --cflags xft libpulse)
LDFLAGS      := $(shell pkg-config --libs x11 xft libpulse) -lpthread
DESTDIR      ?= /usr/local
DISPLAY_NUM  := 69

ifdef DEBUG
	CFLAGS += -ggdb -DDEBUG
endif

ifdef OPTIMIZE
	CFLAGS += -O$(OPTIMIZE)
endif

all: glitch

glitch: main.c logging.c manager.c widgets.c switcher.c audio.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

config.h:
	[ -f config.h ] || cp config.def.h config.h

install: all
	install -Dm755 glitch $(DESTDIR)/bin/glitch

clean:
	rm -f glitch

virt:
	Xephyr -br -ac -noreset -no-host-grab -sw-cursor -screen 1000x1000 :$(DISPLAY_NUM)
