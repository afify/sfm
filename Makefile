# sfm - simple file manager
# See LICENSE file for copyright and license details.

include config.mk

SRC = sfm.c util.c termbox.c utf8.c
OBJ = ${SRC:.c=.o}

all: options sfm

options:
	@echo sfm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	cp config.def.h $@

sfm: ${OBJ}
	${CC} ${LDFLAGS} -o $@ ${OBJ}

clean:
	rm -f sfm ${OBJ} sfm-${VERSION}.tar.gz

dist: clean
	mkdir -p sfm-${VERSION}
	cp -R LICENSE Makefile README.md config.def.h config.mk\
		sfm.1 sfm.png termbox.h util.h ${SRC} sfm-${VERSION}
	tar -cf sfm-${VERSION}.tar sfm-${VERSION}
	gzip sfm-${VERSION}.tar
	rm -rf sfm-${VERSION}

install: sfm
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f sfm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/sfm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < sfm.1 > ${DESTDIR}${MANPREFIX}/man1/sfm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/sfm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/sfm\
		${DESTDIR}${MANPREFIX}/man1/sfm.1

.PHONY: all options clean dist install uninstall
