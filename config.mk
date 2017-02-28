VERSION = 0.1.0

PREFIX = /usr/local

WARNFLAGS := \
        -Werror \
        -Wall -Wextra -pedantic \
        -Wundef \
        -Wno-unused-parameter

INCS := -Ilibutf -Iexpat/lib

#EXTENSION = '.exe'

OPT       = -O3
#COVFLAGS = -ftest-coverage -fprofile-arcs

#STATIC   = -static

COMMON_FLAGS = \
	-pipe \
	-fno-builtin-malloc \
	-fno-omit-frame-pointer \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables \
	-ffunction-sections \
	-fdata-sections \
	-Werror=implicit-function-declaration \
	-Werror=implicit-int \
	-Werror=pointer-sign \
	-Werror=pointer-arith \
#	-fsanitize=address

CPPFLAGS  = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=600
CFLAGS   += $(STATIC) -g -std=c11 $(OPT) -pthread $(WARNFLAGS) $(INCS) $(CPPFLAGS) $(COMMON_FLAGS)
CFLAGS   += -fPIC
#CFLAGS  += -flto
#CFLAGS  += -Wunsafe-loop-optimizations
CFLAGS   += $(COVFLAGS)

LDFLAGS  += $(STATIC) -g $(OPT) -lm $(COVFLAGS)
#LDFLAGS  += -fsanitize=address -lunwind
LDFLAGS  += -Wl,-z,now,-z,relro

#LDFLAGS  +=  -lmesh
LDFLAGS  += -fPIC
LDFLAGS += -flto

LCOV     ?= true #lcov
GENHTML  ?= true #genhtml

AR       ?= llvm-ar
RANLIB   ?= llvm-ranlib

#AR       = gcc-ar
#RANLIB   = gcc-ranlib

#CC      ?= cc
#CC       = /usr/local/musl/bin/musl-gcc
CC        = clang
