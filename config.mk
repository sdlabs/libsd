VERSION = 0.1.0

PREFIX = /usr/local

WARNFLAGS := \
        -Werror \
        -Wall -Wextra -pedantic \
        -Wundef \
        -Wno-unused-parameter

INCS := -Ilibutf -Iexpat/lib

#EXTENSION = '.exe'

OPT       = -Os
#COVFLAGS = -ftest-coverage -fprofile-arcs

STATIC   = -static

CPPFLAGS  = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=600
CFLAGS   += $(STATIC) -g -std=c11 $(OPT) -pthread $(WARNFLAGS) $(INCS) $(CPPFLAGS)
CFLAGS   += -fPIC
#CFLAGS  += -flto
#CFLAGS  += -Wunsafe-loop-optimizations
CFLAGS   += $(COVFLAGS)
CFLAGS   += -fcheckedc-extension

LDFLAGS  += $(STATIC) -g $(OPT) -lm $(COVFLAGS)
LDFLAGS  += -fPIC
#LDFLAGS += -flto

LCOV     ?= true #lcov
GENHTML  ?= true #genhtml

AR       ?= ar
RANLIB   ?= ranlib

#AR       = gcc-ar
#RANLIB   = gcc-ranlib

CC        = clang
#CC      ?= cc
#CC       = /usr/local/musl/bin/musl-gcc
