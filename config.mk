VERSION = 0.1.0

PREFIX=/usr/local

WARNFLAGS := -pedantic -Wvariadic-macros \
             -Wformat -Wall -Wextra -Wundef -Wpointer-arith \
             -Wcast-qual -Wwrite-strings -Wsign-compare \
             -Wstrict-aliasing=2 -Wno-unused-parameter \
             -Werror

INCS := -Ilibutf -Iexpat/lib

OPT := -O0
#COVFLAGS := -ftest-coverage -fprofile-arcs

#STATIC := -static

CPPFLAGS = -DVERSION=\"${VERSION}\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=600
CFLAGS += ${STATIC} -g -std=c11 ${OPT} -pthread -fPIC ${WARNFLAGS} ${INCS} ${CPPFLAGS}
#CFLAGS += -Wunsafe-loop-optimizations
CFLAGS += ${COVFLAGS}

LDFLAGS += ${STATIC} -g ${OPT} -fPIC -lm ${COVFLAGS}

RANLIB ?= ranlib
LCOV ?= true #lcov
GENHTML ?= genhtml
AR ?= ar

#AR ?= gcc-ar
#RANLIB ?= gcc-ranlib

CC ?= cc
#CC := /usr/local/musl/bin/musl-gcc
