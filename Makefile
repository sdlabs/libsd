include config.mk


SRC = util.c xml.c project.c parse.c sim.c
OBJ = $(SRC:.c=.o)

LIB = libsd.a
INC = sd.h
EXE = mdl$(EXTENSION)

HEADERS = sd.h sd_internal.h

CONFIG = Makefile

#TESTS_SRC = test_sd.c
TESTS = sd.test

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS += -s
endif

.SUFFIXES:
.SUFFIXES: .c .o .test

all: check $(EXE)

.c.o: $(CONFIG)
	@echo "  CC    $@"
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

sd.test: test_sd.o $(LIB) $(HEADERS)
	@echo "  LD    $@"
	$(CC) -o $@ test_sd.o $(LIB) $(LDFLAGS)

$(EXE): mdl.o $(LIB) $(HEADERS)
	@echo "  LD    $@"
	$(CC) -o $@ mdl.o $(LIB) $(LDFLAGS)

*.o: config.mk Makefile $(HEADERS)

$(LIB): libutf/libutf.a expat/.libs/libexpat.a sd.h sd_internal.h $(OBJ)
	$(AR) rc $@ expat/lib/*.o
	$(AR) rc $@ libutf/*.o
	$(AR) rc $@ $(OBJ)
	$(RANLIB) $@

expat/Makefile:
	cd expat && ./configure --enable-shared=no --enable-static=yes CC=$(CC) RANLIB=$(RANLIB) AR=$(AR) CFLAGS="-Os -g"

expat/.libs/libexpat.a: expat/Makefile
	$(MAKE) -C expat libexpat.la

libutf/libutf.a:
	$(MAKE) -C libutf

install: $(LIB) $(EXE)
	install -c -m 0644 sd.h $(PREFIX)/include/sd.h
	install -c -m 0644 $(LIB) $(PREFIX)/lib/$(LIB)
	install -c -m 0755 $(EXE) $(PREFIX)/bin/$(EXE)

check: $(TESTS) $(EXE)
	@echo "  TEST  $(TESTS)"
	$(LCOV) --directory . --zerocounters 2>/dev/null
	./$(TESTS)
	./regression-tests.py ./$(EXE) test/test-models

coverage: check
	mkdir -p out
        # dont include unit test files in code coverage reports
	rm -f test_*.gc*
	cd out; $(LCOV) --directory .. --capture --output-file app.info && $(GENHTML) app.info

clean:
	rm -f $(LIB) *.o $(TESTS) $(EXE) *.gcda *.gcno *.d
	rm -rf out
	$(MAKE) -C libutf clean
	$(MAKE) -C expat clean

-include $(OBJS:.o=.d)

.PHONY: all clean check coverage install
