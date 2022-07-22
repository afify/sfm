/* See LICENSE file for copyright and  license details. */

#ifndef UTF8_H
#define UTF8_H

#define UTF_SIZ       4
#define UTF_INVALID   0xFFFD

typedef uint_least32_t Rune;
typedef unsigned char uchar;

size_t utf8validate(Rune *, size_t);
size_t utf8encode(Rune, char *);
char utf8encodebyte(Rune, size_t);
#endif /* UTF8_H */
