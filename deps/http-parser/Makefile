# Copyright Joyent, Inc. and other Node contributors. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

#PLATFORM ?= $(shell sh -c 'uname -s | tr "[A-Z]" "[a-z]"')

#CC?=gcc
CC=cl.exe
#AR?=ar
AR=lib.exe

LD=link.exe

#CPPFLAGS ?= -g
#CPPFLAGS ?= -c -MD -Zi -EHsc -GR -DWIN32 -DWIN64 -D_WIN64 -TP -bigobj -Gs -Gy -GF -DWINDOWS -D_AMD64
CPPFLAGS ?= -c -MD -Zi -EHsc -GR -DWIN32 -DWIN64 -D_WIN64 -TP

CPPFLAGS += -I.
CPPFLAGS_DEBUG = $(CPPFLAGS) -DHTTP_PARSER_STRICT=1
CPPFLAGS_DEBUG += $(CPPFLAGS_DEBUG_EXTRA)
CPPFLAGS_FAST = $(CPPFLAGS) -DHTTP_PARSER_STRICT=0
CPPFLAGS_FAST += $(CPPFLAGS_FAST_EXTRA)
CPPFLAGS_BENCH = $(CPPFLAGS_FAST)

#CFLAGS += -Wall -Wextra -Werror
#CFLAGS_DEBUG = $(CFLAGS) -O0 -g $(CFLAGS_DEBUG_EXTRA)
#CFLAGS_FAST = $(CFLAGS) -O3 $(CFLAGS_FAST_EXTRA)
#CFLAGS_BENCH = $(CFLAGS_FAST) -Wno-unused-parameter
#CFLAGS_LIB = $(CFLAGS_FAST) -fPIC

#LDFLAGS ?=
LDFLAGS ?= -debug
#LDFLAGS_LIB = $(LDFLAGS) -shared
LDFLAGS_SHARED = $(LDFLAGS) -dll
#LDFLAGS_EXE = $(LDFLAGS) -subsystem:console -debug /OPT:ICF /OPT:REF -DYNAMICBASE -NXCOMPAT advapi32.lib
LDFLAGS_EXE = $(LDFLAGS) -subsystem:console

HELPER ?=
#SONAME ?= libhttp_parser.so.2.6.0
LIBNAME_SHARED ?= http_parser.dll
LIBNAME_STATIC ?= http_parser.lib
#SOEXT ?= so

INSTALL ?= install
DESTDIR ?= ./output
PREFIX ?= $(DESTDIR)
LIBDIR_SHARED = $(PREFIX)/shared_lib
LIBDIR_STATIC = $(PREFIX)/static_lib
INCLUDEDIR = $(PREFIX)/include

##ifneq (darwin,$(PLATFORM))
# TODO(bnoordhuis) The native SunOS linker expects -h rather than -soname...
#LDFLAGS_LIB += -Wl,-soname=$(SONAME)
##endif

all: test-run library_shared library_static

test_g.exe: http_parser_g.o test_g.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'
test_g.o: test.c http_parser.h
	$(CC) $(CPPFLAGS_DEBUG) test.c -Fo$@
http_parser_g.o: http_parser.c http_parser.h
	$(CC) $(CPPFLAGS_DEBUG) http_parser.c -Fo$@
test_fast.exe: http_parser.o test.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'
test.o: test.c http_parser.h
	$(CC) $(CPPFLAGS_FAST) test.c -Fo$@
http_parser.o: http_parser.c http_parser.h
	$(CC) $(CPPFLAGS_FAST) http_parser.c -Fo$@

bench.exe: http_parser.o bench.o
	$(LD) $(LDFLAGS_EXE) http_parser.o bench.o -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'
bench.o: bench.c http_parser.h
	$(CC) $(CPPFLAGS_BENCH) bench.c -Fo$@

test-run: test_g.exe test_fast.exe
	$(HELPER) ./test_g.exe
	$(HELPER) ./test_fast.exe
test-run-timed: test_fast.exe
	while(true) do time $(HELPER) ./test_fast.exe > /dev/null; done
#test-valgrind: test_g.exe
#	valgrind ./test_g.exe

url_parser.o: contrib/url_parser.c http_parser.h
	$(CC) $(CPPFLAGS_FAST) contrib/url_parser.c -Fo$@
url_parser.exe: http_parser.o url_parser.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'
url_parser_g.exe: http_parser_g.o url_parser.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'

parsertrace.o: contrib/parsertrace.c http_parser.h
	$(CC) $(CPPFLAGS_FAST) contrib/parsertrace.c -Fo$@
parsertrace.exe: http_parser.o parsertrace.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'
parsertrace_g.exe: http_parser_g.o parsertrace.o
	$(LD) $(LDFLAGS_EXE) $^ -out:$@
	mt.exe -manifest $@.manifest -outputresource:'$@;1'

#libhttp_parser.o: http_parser.c http_parser.h
#	$(CC) $(CPPFLAGS_FAST) http_parser.c -Folibhttp_parser.o
library_shared: http_parser.o
	$(LD) $(LDFLAGS_SHARED) -out:$(LIBNAME_SHARED) $<
	mt.exe -manifest $(LIBNAME_SHARED).manifest -outputresource:'$(LIBNAME_SHARED);2'
library_static: http_parser.o
	$(AR) /out:$(LIBNAME_STATIC) $<

tags: http_parser.c http_parser.h test.c
#	ctags $^

install: library_shared library_static
	$(INSTALL) -D  http_parser.h $(INCLUDEDIR)/http_parser.h
	$(INSTALL) -D $(LIBNAME_SHARED) $(LIBDIR_SHARED)/$(LIBNAME_SHARED)
	$(INSTALL) -D $(LIBNAME_STATIC) $(LIBDIR_STATIC)/$(LIBNAME_STATIC)

install-strip: library_shared library_static
#	$(INSTALL) -D  http_parser.h $(INCLUDEDIR)/http_parser.h
#	$(INSTALL) -D -s $(SONAME) $(LIBDIR)/$(SONAME)

uninstall:
	rm -rf $(INCLUDEDIR)/http_parser.h
	rm -rf $(LIBDIR_SHARED)/$(LIBNAME_SHARED)
	rm -rf $(LIBDIR_STATIC)/$(LIBNAME_STATIC)

clean:
	rm -rf *.exp *.ilk *.manifest *.lib *.pdb *.o tags
	rm -rf $(LIBNAME_SHARED) $(LIBNAME_STATIC)
	rm -rf test_fast.exe test_g.exe url_parser.exe url_parser_g.exe
	rm -rf parsertrace parsertrace_g.exe
	rm -rf $(DESTDIR)

#contrib/url_parser.c:	http_parser.h
#contrib/parsertrace.c:	http_parser.h

.PHONY: all clean library_shared library_static test-run test-run-timed test-valgrind install install-strip uninstall
