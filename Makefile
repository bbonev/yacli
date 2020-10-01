############################# configurable section #############################

# configure install path

PREFIX?=/usr/local
LIBDIR?=/lib/
INCDIR?=/include/

# configure debug or release build

# for debug build, uncomment the line below
#DEBUG?=-DDEBUG=1 -O0 -g3 -fno-inline -fstack-protector-all
DEBUG?=-O3

PKG_CONFIG?=pkg-config
YASCC?=$(shell $(PKG_CONFIG) --cflags yascreen)
YASLD?=$(shell $(PKG_CONFIG) --libs yascreen)
ifeq ("$(YASLD)","")
YASCC:=-lyascreen
YASLD:=
endif

# configure default tools

# make defines AR, so we have to override to gcc-ar, so -flto works
AR=gcc-ar
RANLIB?=gcc-ranlib

########################## end of configurable section #########################

VER=$(shell grep Revision yacli.c|head -n1|sed -e 's/.\+Revision: \([0-9.]\+\) \+.\+/\1/'|tr . ' '|awk '{printf "%i.%02u\n",$$1+$$2/100,$$2%100}')

# change options based on wild guess of compiler brand/type

ifeq ($(lastword $(subst /, ,$(CC))),tcc)
CCOPT:=-Wall $(DEBUG) -I.
STLINK:=-L. -static -lyascreen
else
ifeq ($(lastword $(subst /, ,$(CC))),clang)
CCOPT:=-Wall $(DEBUG) -I. --std=gnu89
STLINK:=-L. -lyascreen
else
CCOPT:=-Wall $(DEBUG) -I. --std=gnu89 -flto
STLINK:=-L. -lyascreen
endif
endif

ifeq ($(shell uname -s),OpenBSD)
ifeq ($(CC),cc)
CC:=egcc
endif
AR=ar
RANLIB=ranlib
CCOPT:=-Wall $(DEBUG) -I. --std=gnu89
endif

# shared library version

SOVERM:=0
SOVERF:=0.0.0

# allow to pass additional compiler flags

MYCFLAGS=$(DEBUG) $(CPPFLAGS) $(CFLAGS) $(YASCC) $(CCOPT)
MYLDFLAGS=$(LDFLAGS) $(YASLD) $(LDOPT)

all: libyacli.a libyacli.so yacli.pc

yacli.o: yacli.c yacli.h
	$(CC) $(MYCFLAGS) -o $@ -c $<

yaclitest.o: yaclitest.c yacli.h
	$(CC) $(MYCFLAGS) -o $@ -c $<

yaclitest: yaclitest.o yacli.o
	$(CC) $(MYCFLAGS) -o $@ $^ $(STLINK)

libyacli.a: yacli.o
	$(AR) r $@ $^
	$(RANLIB) $@

libyacli.so: libyacli.so.$(SOVERM)
	ln -sf $^ $@

libyacli.so.$(SOVERM): libyacli.so.$(SOVERF)
	ln -sf $^ $@

libyacli.so.$(SOVERF): yacli.c yacli.h
	$(CC) $(MYCFLAGS) -o $@ $< -fPIC -shared -Wl,--version-script,yacli.vers -Wl,-soname,libyacli.so.$(SOVERM) $(MYLDFLAGS)

yacli.pc: yacli.pc.in
	sed \
		-e 's|YACLIVERSION|$(VER)|' \
		-e 's|YACLIPREFIX|$(PREFIX)|' \
		-e 's|YACLILIBDIR|$(PREFIX)$(LIBDIR)|' \
		-e 's|YACLIINCDIR|$(PREFIX)$(INCDIR)|' \
	< $< > $@

install: libyacli.a libyacli.so yacli.pc
	$(INSTALL) -Ds -m 644 -t $(DESTDIR)$(PREFIX)$(LIBDIR) libyacli.a
	$(INSTALL) -Ds -m 644 -t $(DESTDIR)$(PREFIX)$(LIBDIR)/pkgconfig/ yacli.pc
	ln -fs libyacli.so.$(SOVERF) $(DESTDIR)$(PREFIX)$(LIBDIR)libyacli.so.$(SOVERM)
	ln -fs libyacli.so.$(SOVERM) $(DESTDIR)$(PREFIX)$(LIBDIR)libyacli.so
	$(INSTALL) -Ds -m 644 -s -t $(DESTDIR)$(PREFIX)$(LIBDIR) libyacli.so.$(SOVERF)
	$(INSTALL) -Ds -m 644 -t $(DESTDIR)$(PREFIX)$(INCDIR) yacli.h
	-#$(INSTALL) -TDs -m 0644 yacli.3 $(DESTDIR)$(PREFIX)/share/man/man3/yacli.3

clean:
	rm -f yaclitest yaclitest.shared yaclitest.o yacli.o libyacli.a libyacli.so libyacli.so.$(SOVERM) libyacli.so.$(SOVERF) yacli.pc

rebuild:
	$(MAKE) clean
	$(MAKE) -j all

mkotar:
	$(MAKE) clean
	-dh_clean
	#$(MAKE) yacli.3
	tar \
		--xform 's,^[.],yacli-$(VER),' \
		--exclude ./.git \
		--exclude ./.gitignore \
		--exclude ./.cvsignore \
		--exclude ./CVS \
		--exclude ./debian \
		-Jcvf ../yacli_$(VER).orig.tar.xz .
	-rm -f ../yacli_$(VER).orig.tar.xz.asc
	gpg -a --detach-sign ../yacli_$(VER).orig.tar.xz
	cp -fa ../yacli_$(VER).orig.tar.xz ../yacli-$(VER).tar.xz
	cp -fa ../yacli_$(VER).orig.tar.xz.asc ../yacli-$(VER).tar.xz.asc

.PHONY: install clean rebuild all
