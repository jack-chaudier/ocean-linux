/*
 * Ocean libc - Standard Definitions
 */

#ifndef _STDDEF_H
#define _STDDEF_H

/* Null pointer constant */
#define NULL ((void *)0)

/* Size types */
typedef unsigned long       size_t;
typedef long                ssize_t;
typedef long                ptrdiff_t;

/* Wide character type */
typedef int                 wchar_t;

/* Offset of member in structure */
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
