include config.mk

SRC = util.c xml.c project.c parse.c sim.c
OBJ = ${SRC:.c=.o}

LIB = libsd.a
INC = sd.h
EXE = mdl

HEADERS = sd.h sd_internal.h

#TESTS_SRC = test_sd.c
TESTS = sd.test

.SUFFIXES:
.SUFFIXES: .c .o .test

all: check ${EXE}

.c.o:
	${CC} -c ${CFLAGS} $<

sd.test: test_sd.o ${LIB} ${HEADERS}
	${CC} -o $@ test_sd.o ${LIB} ${LDFLAGS}

${EXE}: mdl.o ${LIB} ${HEADERS}
	${CC} -o $@ mdl.o ${LIB} ${LDFLAGS}

*.o: config.mk Makefile ${HEADERS}

${LIB}: libutf/libutf.a expat/.libs/libexpat.a sd.h sd_internal.h ${OBJ}
	${AR} rc $@ expat/lib/*.o
	${AR} rc $@ libutf/*.o
	${AR} rc $@ ${OBJ}
	${RANLIB} $@

expat/Makefile:
	cd expat && ./configure --enable-shared=no --enable-static=yes CC=${CC} RANLIB=${RANLIB} AR=${AR} CFLAGS="-Os -g -fPIC"

expat/.libs/libexpat.a: expat/Makefile
	${MAKE} -C expat libexpat.la

libutf/libutf.a:
	${MAKE} -C libutf

install: ${LIB} ${EXE}
	install -c -m 0644 sd.h ${PREFIX}/include/sd.h
	install -c -m 0644 ${LIB} ${PREFIX}/lib/${LIB}
	install -c -m 0755 ${EXE} ${PREFIX}/bin/${EXE}

check: ${TESTS}
	${LCOV} --directory . --zerocounters 2>/dev/null
	./${TESTS}

coverage: check
	mkdir -p out
        # dont include unit test files in code coverage reports
	rm -f test_*.gc*
	cd out; ${LCOV} --directory .. --capture --output-file app.info && ${GENHTML} app.info

clean:
	rm -f ${LIB} *.o ${TESTS} ${EXE} *.gcda *.gcno
	rm -rf out
	${MAKE} -C libutf clean
	${MAKE} -C expat clean

.PHONY: all clean check coverage install
