# sfm version
VERSION = 0.1

# paths
PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man

# flags
CPPFLAGS = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -Os ${CPPFLAGS}
LDFLAGS  = -lpthread -s

# compiler and linker
CC = cc
