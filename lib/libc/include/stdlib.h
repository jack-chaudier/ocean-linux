/*
 * Ocean libc - Standard Library
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/* Process termination */
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

/* String conversion */
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

/* Memory allocation (not implemented yet) */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* Miscellaneous */
int abs(int j);
long labs(long j);

#endif /* _STDLIB_H */
