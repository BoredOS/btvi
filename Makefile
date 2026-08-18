CC = x86_64-boredos-gcc

DESTDIR ?= $(abspath build/dist)

CFLAGS  = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone \
          -Iinclude -D_GNU_SOURCE -DPREFIX=\"/usr\" \
          -DHAVE_TERMIOS_H -DHAVE_SYS_IOCTL_H -DHAVE_UNISTD_H -DHAVE_SIGNAL_H \
          -DHAVE_POLL_H -DHAVE_SIGACTION -DHAVE_SIGEMPTYSET -DHAVE_ISATTY -DHAVE_READ

LDFLAGS = -static -no-pie -Wl,-Ttext=0x40000000 \
          -Wl,--no-dynamic-linker -Wl,-z,text -Wl,-z,max-page-size=0x1000

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, obj/%.o, $(SRCS))

APPS = tvi.elf

all: $(APPS)

$(APPS): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

obj/%.o: src/%.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

install: all
	mkdir -p $(DESTDIR)/bin
	cp $(APPS) $(DESTDIR)/bin/
	mkdir -p $(DESTDIR)/usr/share/doc/tvi
	if [ -f help.txt ]; then cp help.txt $(DESTDIR)/usr/share/doc/tvi/; fi
	if [ -f README.md ]; then cp README.md $(DESTDIR)/usr/share/doc/tvi/; fi
	if [ -f COPYING.txt ]; then cp COPYING.txt $(DESTDIR)/usr/share/doc/tvi/; fi

.PHONY: bup
bup: all
	rm -rf build/package
	mkdir -p build/package/bin
	mkdir -p build/package/assets
	cp $(APPS) build/package/bin/
	if [ -f help.txt ]; then cp help.txt build/package/assets/; fi
	if [ -f README.md ]; then cp README.md build/package/assets/; fi
	if [ -f COPYING.txt ]; then cp COPYING.txt build/package/assets/; fi
	cp MANIFEST.toml build/package/
	x86_64-boredos-strip --strip-unneeded build/package/bin/*.elf 2>/dev/null || true
	mkdir -p build
	tar -cf build/btvi.tar -C build/package MANIFEST.toml bin assets
	lz4 -f build/btvi.tar build/btvi.bup
	rm -f build/btvi.tar
	rm -rf build/package

clean:
	rm -rf obj build $(APPS)
