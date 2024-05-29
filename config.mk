# sfm version
VERSION = 0.5

# paths
PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man
DEBUG     = -g3

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L
CPPFLAGS += -D_XOPEN_SOURCE=700 -DVERSION=\"${VERSION}\"
CPPFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3

CFLAGS  = -std=c99 -pedantic
#CFLAGS  += -Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  += -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes \
	-Wwrite-strings -Wunused-variable -Wformat -Wformat-security \
	-Werror=format-security -Wcast-align -Wno-unused-parameter \
	-Wno-pointer-sign
CFLAGS  += -Os -pipe
CFLAGS  += -fstack-protector-strong \
	-fPIE -fcf-protection -fno-omit-frame-pointer \
	-mno-omit-leaf-frame-pointer -fno-plt
CFLAGS  += -flto
CFLAGS  += ${CPPFLAGS}
CFLAGS  += ${DEBUG}

#CFLAGS  += -fsanitize=undefined
#CFLAGS  += -fsanitize=leak
#CFLAGS  += -fsanitize=address
#CFLAGS  += -fsanitize=thread
#CFLAGS  += -fstack-clash-protection

# CFLAGS  += -fsanitize=cfi -fvisibility=hidden #clang only

LDFLAGS  = -pthread -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -pie -flto
# compiler and linker
CC = clang
