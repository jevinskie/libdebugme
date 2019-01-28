# Copyright 2016-2017 Yury Gribov
# 
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.txt file.

CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -fPIC -g -fvisibility=hidden -Wall -Wextra -Werror
LDFLAGS = -fPIC -shared -Wl,--no-allow-shlib-undefined
LIBS += -ldl
ifeq (,$(DEBUG))
  CFLAGS += -O2
  LDFLAGS += -Wl,-O2
else
  CFLAGS += -O0
endif
ifneq (,$(ASAN))
  CFLAGS += -fsanitize=address
  LDFLAGS += -Wl,--allow-shlib-undefined -fsanitize=address
endif
ifneq (,$(UBSAN))
  CFLAGS += -fsanitize=undefined
  LDFLAGS += -fsanitize=undefined
endif

DESTDIR = /usr

OBJS32 = bin/gdb-32.o bin/debugme-32.o bin/init-32.o bin/common-32.o
OBJS64 = bin/gdb-64.o bin/debugme-64.o bin/init-64.o bin/common-64.o

$(shell mkdir -p bin)

all: bin/libdebugme-32.so bin/libdebugme-64.so

install:
	$(error install not supported with 32/64 bit build hack)
	install -D bin/libdebugme.so $(DESTDIR)/lib

DEBUGME_OPTIONS = handle_signals=1:quiet=1:altstack=1:debug_opts=-quiet -batch -ex backtrace

check:
	$(error check not supported with 32/64 bit build hack)
	$(CC) $(CPPFLAGS) test/segv.c -o bin/a.out
	if DEBUGME_OPTIONS='$(DEBUGME_OPTIONS)' LD_PRELOAD=bin/libdebugme.so bin/a.out; then false; fi
	$(CC) $(CPPFLAGS) test/segv.c -Wl,--no-as-needed bin/libdebugme.so -o bin/a.out
	if DEBUGME_OPTIONS='$(DEBUGME_OPTIONS)' LD_LIBRARY_PATH=bin bin/a.out; then false; fi

bin/libdebugme-32.so: $(OBJS32) bin/FLAGS Makefile
	$(CC) -m32 $(LDFLAGS) $(OBJS32) $(LIBS) -o $@

bin/libdebugme-64.so: $(OBJS64) bin/FLAGS Makefile
	$(CC) -m64 $(LDFLAGS) $(OBJS64) $(LIBS) -o $@

bin/%-32.o: src/%.c Makefile bin/FLAGS
	$(CC) -m32 $(CPPFLAGS) $(CFLAGS) -c $< -o $@

bin/%-64.o: src/%.c Makefile bin/FLAGS
	$(CC) -m64 $(CPPFLAGS) $(CFLAGS) -c $< -o $@


bin/FLAGS: FORCE
	if test x"$(CFLAGS) $(LDFLAGS)" != x"$$(cat $@)"; then \
		echo "$(CFLAGS) $(LDFLAGS)" > $@; \
	fi

help:
	@echo "Common targets:"
	@echo "  all        Build all executables and scripts"
	@echo "  clean      Clean all build files and temps."
	@echo "  help       Print help on build options."
	@echo '  install    Install to $$DESTDIR (default is /usr).'
	@echo ""
	@echo "Less common:"
	@echo "  check      Run regtests."
	@echo ""
	@echo "Build options:"
	@echo "  DESTDIR=path  Specify installation root."
	@echo "  DEBUG=1       Build debug version of code."
	@echo "  ASAN=1        Build with ASan checks."
	@echo "  UBSAN=1       Build with UBSan checks."

clean:
	rm -f bin/*

.PHONY: clean all install check FORCE help

