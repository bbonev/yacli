############################# configurable section #############################

# configure install path

PREFIX:=/usr/local

# configure debug or release build

DEBUG:=-DDEBUG=1 -O0 -g3 -fno-inline -fstack-protector-all
# for debug build, comment the line below
DEBUG:=-O3

# configure default tools

CC?=cc
# make defines AR, so we have to override to gcc-ar, so -flto works
AR=gcc-ar
STRIP?=strip
RANLIB?=gcc-ranlib

########################## end of configurable section #########################

# shared library version

SOVERM:=0
SOVERF:=0.0.0

# change options based on wild guess of compiler brand/type

ifeq ($(lastword $(subst /, ,$(CC))),tcc)
CCOPT:=-Wall $(DEBUG) -I.
STLINK:=-L. -static -lyascreen
DYLINK:=-L. -lyacli -lyascreen
else
ifeq ($(lastword $(subst /, ,$(CC))),clang)
CCOPT:=-Wall $(DEBUG) -I. --std=gnu89
STLINK:=-L. -Wl,-Bstatic -lyascreen -Wl,-Bdynamic
DYLINK:=-L. -lyacli -lyascreen
else
CCOPT:=-Wall $(DEBUG) -I. --std=gnu89 -flto
STLINK:=-L. -Wl,-Bstatic -lyascreen -Wl,-Bdynamic
DYLINK:=-L. -lyacli -lyascreen
endif
endif

# allow to pass additional compiler flags

CFLAGS:=$(CCOPT) $(CFLAGS)

all: libyacli.a libyacli.so yaclitest yaclitest.shared

yacli.o: yacli.c yacli.h
	$(CC) $(CFLAGS) -o $@ -c $<

yaclitest.o: yaclitest.c yacli.h
	$(CC) $(CFLAGS) -o $@ -c $<

yaclitest: yaclitest.o yacli.o
	$(CC) $(CFLAGS) -o $@ $^ -lrt $(STLINK)
	$(STRIP) $@

yaclitest.shared: yaclitest.o libyacli.so
	$(CC) $(CFLAGS) -o $@ $< -lrt $(DYLINK)
	$(STRIP) $@

libyacli.a: yacli.o
	$(AR) r $@ $^
	$(RANLIB) $@

libyacli.so: libyacli.so.$(SOVERM)
	ln -sf $^ $@

libyacli.so.$(SOVERM): libyacli.so.$(SOVERF)
	ln -sf $^ $@

libyacli.so.$(SOVERF): yacli.c yacli.h
	$(CC) $(CFLAGS) -o $@ $< -fPIC -lrt -shared
	$(STRIP) $@

install: all
	cp -a libyacli.a libyacli.so libyacli.so.$(SOVERM) libyacli.so.$(SOVERF) $(PREFIX)/lib/
	cp -a yacli.h $(PREFIX)/include/

clean:
	rm -f yaclitest yaclitest.shared yaclitest.o yacli.o libyacli.a libyacli.so libyacli.so.$(SOVERM) libyacli.so.$(SOVERF)

rebuild:
	$(MAKE) clean
	$(MAKE) all

.PHONY: install clean rebuild all
