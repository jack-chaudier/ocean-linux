/*
 * Ocean Kernel - Basic Type Definitions
 *
 * Provides fixed-width integer types, size types, and common typedefs
 * for the kernel. Uses compiler's stdint.h for standard types.
 */

#ifndef _OCEAN_TYPES_H
#define _OCEAN_TYPES_H

/* Use compiler's stdint.h for standard fixed-width types (freestanding OK) */
#include <stdint.h>

/* Shorthand type aliases */
typedef int8_t              i8;
typedef uint8_t             u8;
typedef int16_t             i16;
typedef uint16_t            u16;
typedef int32_t             i32;
typedef uint32_t            u32;
typedef int64_t             i64;
typedef uint64_t            u64;

/* Size types - use compiler definitions if available */
#ifndef __SIZE_TYPE__
typedef uint64_t            size_t;
#else
typedef __SIZE_TYPE__       size_t;
#endif

typedef int64_t             ssize_t;

/* Pointer difference type */
#ifndef __PTRDIFF_TYPE__
typedef int64_t             ptrdiff_t;
#else
typedef __PTRDIFF_TYPE__    ptrdiff_t;
#endif

/* Boolean type */
typedef _Bool               bool;
#define true                1
#define false               0

/* Null pointer */
#define NULL                ((void *)0)

/* Process/thread IDs */
typedef int32_t             pid_t;
typedef int32_t             tid_t;

/* User/group IDs */
typedef uint32_t            uid_t;
typedef uint32_t            gid_t;

/* File/device types */
typedef int32_t             mode_t;
typedef int64_t             off_t;
typedef uint32_t            dev_t;
typedef uint64_t            ino_t;

/* Physical address type */
typedef uint64_t            phys_addr_t;

/* Page frame number */
typedef uint64_t            pfn_t;

/* Capability handle */
typedef uint32_t            cap_t;

/* Additional size limits if not defined */
#ifndef SIZE_MAX
#define SIZE_MAX            UINT64_MAX
#endif
#define SSIZE_MAX           INT64_MAX

/* Printf format specifiers for fixed-width types */
#define PRId8               "d"
#define PRId16              "d"
#define PRId32              "d"
#define PRId64              "lld"
#define PRIu8               "u"
#define PRIu16              "u"
#define PRIu32              "u"
#define PRIu64              "llu"
#define PRIx8               "x"
#define PRIx16              "x"
#define PRIx32              "x"
#define PRIx64              "llx"
#define PRIX64              "llX"

/* Variadic function support */
typedef __builtin_va_list   va_list;
#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_copy(dest, src)  __builtin_va_copy(dest, src)

#endif /* _OCEAN_TYPES_H */
