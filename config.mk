# sfm version
VERSION = 0.5

# paths
PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man
DEBUG     = -g3

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DVERSION=\"${VERSION}\"

CFLAGS   = ${DEBUG} \
	-std=c99 \
	-pedantic \
	-Wextra \
	-Wall \
	-Wformat \
	-Wformat-security \
	-Wcast-align \
	-Wno-unused-parameter \
	-Os ${CPPFLAGS} \
	-fstack-protector-strong \
	-D_FORTIFY_SOURCE=2 \
	-fPIE

LDFLAGS  = -pthread -Wl,-z,relro -Wl,-z,now

#LDFLAGS  = -pthread

# compiler and linker
CC = cc
