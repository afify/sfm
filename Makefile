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
	splint +unixlib -weak +enumint -preproc -D__FreeBSD__ -DVERSION=\"$(VERSION)\" $(SRC)

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

perf: $(BIN)
	@echo "Running performance analysis..."
	@echo "Building with profiling flags..."
	$(CC) -c -pg -O2 -std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DVERSION=\"$(VERSION)\" $(SRC)
	$(CC) -pg $(LDFLAGS) -o $(BIN) $(OBJ)
	@echo "Run './sfm' with your workload, then use 'gprof sfm gmon.out' to see profile"

perf-record: $(BIN)
	@echo "Recording performance with perf (requires root or perf_event_paranoid=1)..."
	timeout 5 perf record -g -- script -qec "printf 'jjjjjkkkkGggGq' | ./$(BIN)" /dev/null || true
	@echo "Analyzing perf data..."
	perf report --stdio | head -50

security: valgrind cppcheck clang-tidy

.PHONY: all options clean dist install uninstall perf perf-record
