/* See LICENSE file for copyright and license details. */
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

void 
float_to_string(float f, char *r)
{
    long long int length, length2, i, number, position, sign, tenth;
    float number2;

	f = floorf(f * 100) / 100;

    sign = -1;   // -1 == positive number
    if (f < 0)
    {
        sign = '-';
        f *= -1;
    }

    number2 = f;
    number = f;
    length = 0;  // Size of decimal part
    length2 = 0; // Size of tenth

    /* Calculate length2 tenth part */
    while( (number2 - (float)number) != 0.0 && !((number2 - (float)number) < 0.0) )
    {
		tenth = n_tu(10.0, length2 + 1);
        number2 = f * tenth;
        number = number2;

        length2++;
    }
	
    /* Calculate length decimal part */
    for (length = (f > 1) ? 0 : 1; f > 1; length++)
        f /= 10;

    position = length;
    length = length + 1 + length2;
    number = number2;

    if (sign == '-')
    {
        length++;
        position++;
    }
	
	if (length2 > 0) {
		for (i = length; i >= 0 ; i--)
		{
			if (i == (length))
				r[i] = '\0';
			else if(i == (position))
				r[i] = '.';
			else if(sign == '-' && i == 0)
				r[i] = '-';
			else
			{
				r[i] = (number % 10) + '0';
				number /=10;
				
			}
		}
	} else {
		length--;
		for (i = length; i >= 0 ; i--)
		{
			if (i == (length))
				r[i] = '\0';
			else if(sign == '-' && i == 0)
				r[i] = '-';
			else
			{
				r[i] = (number % 10) + '0';
				number /=10;
			}
		}
	}
}