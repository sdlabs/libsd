VERSION = 0.1.0

PREFIX = /usr/local

WARNFLAGS := \
        -Werror \
        -Wall -Wextra -pedantic \
        -Wundef \
        -Wno-unused-parameter

INCS := -Ilibutf -Iexpat/lib

#EXTENSION = '.exe'

OPT := -O0
#COVFLAGS := -ftest-coverage -fprofile-arcs

#STATIC := -static

CPPFLAGS = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=600
CFLAGS += $(STATIC) -g -std=c11 $(OPT) -pthread $(WARNFLAGS) $(INCS) $(CPPFLAGS)
CFLAGS += -fPIC
#CFLAGS += -Wunsafe-loop-optimizations
CFLAGS += $(COVFLAGS)

LDFLAGS += $(STATIC) -g $(OPT) -lm $(COVFLAGS)
LDFLAGS += -fPIC

RANLIB ?= ranlib
LCOV ?= true #lcov
GENHTML ?= genhtml
AR ?= ar

#AR ?= gcc-ar
#RANLIB ?= gcc-ranlib

CC ?= cc
#CC := /usr/local/musl/bin/musl-gcc
