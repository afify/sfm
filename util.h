/* See LICENSE file for copyright and license details. */

#ifndef UTIL_H
#define UTIL_H

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define LEN(A)                  (sizeof(A)/sizeof(A[0]))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))
#define FAIL_IF(EXP, MSG)       {if(EXP){fprintf(stderr, "[\033[31mFAILED %d\033[0m] %s\n", __LINE__, MSG);exit(EXIT_FAILURE);}}
#define PERROR(EXP)              {if(EXP){print_error(strerror(errno));}};

void *ecalloc(size_t, size_t);
void *erealloc(void*, size_t);
void die(const char *fmt, ...);

#endif /* UTIL_H */
