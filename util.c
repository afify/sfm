/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	p = calloc(nmemb, size);
	FAIL_IF(p == NULL, "calloc");
	return p;
}

void *
erealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));
	return p;
}

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] != '\0' && fmt[strlen(fmt)-1] == ':') {
		(void)fputc(' ', stderr);
		perror(NULL);
	} else {
		(void)fputc('\n', stderr);
	}

	exit(EXIT_FAILURE);
}
