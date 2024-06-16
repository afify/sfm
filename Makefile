# sfm - simple file manager
# See LICENSE file for copyright and license details.

include config.mk

BIN = sfm
SRC = ${BIN}.c
OBJ = ${SRC:.c=.o}

all: options ${BIN}

options:
	@echo ${BIN} build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	cp config.def.h $@

${BIN}: ${OBJ}
	${CC} ${LDFLAGS} -o $@ ${OBJ}

clean:
	rm -f ${BIN} ${OBJ} ${BIN}-${VERSION}.tar.gz

dist: clean
	mkdir -p ${BIN}-${VERSION}
	cp -R LICENSE Makefile README.md config.def.h config.mk\
		${BIN}.1 ${BIN}.png ${SRC} ${BIN}-${VERSION}
	tar -cf ${BIN}-${VERSION}.tar ${BIN}-${VERSION}
	gzip ${BIN}-${VERSION}.tar
	rm -rf ${BIN}-${VERSION}

install: ${BIN}
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${BIN} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${BIN}
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < ${BIN}.1 > ${DESTDIR}${MANPREFIX}/man1/${BIN}.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/${BIN}.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${BIN}\
		${DESTDIR}${MANPREFIX}/man1/${BIN}.1

valgrind: $(BIN)
	@echo "Running valgrind..."
	valgrind \
		--track-origins=yes \
		--leak-check=full \
		--show-leak-kinds=all \
		--trace-children=yes \
		--expensive-definedness-checks=yes \
		--undef-value-errors=yes \
		-s ./$(BIN)

splint: $(SRC)
	@echo "Running splint..."
	splint $(SRC)

cppcheck:
	@echo "Running cppcheck..."
	cppcheck \
		--enable=all \
		--inconclusive \
		--std=c99 \
		--language=c \
		--suppress=missingIncludeSystem ./${SRC}

clang-tidy:
	@echo "Running clang-tidy..."
	clang-tidy $(SRC) -- $(CFLAGS)

security: valgrind cppcheck clang-tidy

.PHONY: all options clean dist install uninstall
