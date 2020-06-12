# sfm version
VERSION = 0.1

# Customize below to fit your system

# paths
PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man

#includes and libs
STD  = -std=c99
WARN = -pedantic -Wextra -Wall

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DVERSION=\"${VERSION}\"
CFLAGS   = ${STD} ${WARN} -Os ${CPPFLAGS}

# compiler and linker
CC = cc
