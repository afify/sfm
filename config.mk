# sfm version
VERSION = 0.5

# paths
PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DVERSION=\"${VERSION}\"
CFLAGS   = -g3 -std=c99 -pedantic -Wextra -Wall -Wno-unused-parameter -Os ${CPPFLAGS}
LDFLAGS  = -pthread

# compiler and linker
CC = cc
