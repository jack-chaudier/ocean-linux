/*
 * Ocean libc - Standard Integer Types
 */

#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width integer types */
typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* Minimum-width integer types */
typedef int8_t              int_least8_t;
typedef int16_t             int_least16_t;
typedef int32_t             int_least32_t;
typedef int64_t             int_least64_t;

typedef uint8_t             uint_least8_t;
typedef uint16_t            uint_least16_t;
typedef uint32_t            uint_least32_t;
typedef uint64_t            uint_least64_t;

/* Fastest minimum-width integer types */
typedef int8_t              int_fast8_t;
typedef int64_t             int_fast16_t;
typedef int64_t             int_fast32_t;
typedef int64_t             int_fast64_t;

typedef uint8_t             uint_fast8_t;
typedef uint64_t            uint_fast16_t;
typedef uint64_t            uint_fast32_t;
typedef uint64_t            uint_fast64_t;

/* Integer types capable of holding object pointers */
typedef long                intptr_t;
typedef unsigned long       uintptr_t;

/* Greatest-width integer types */
typedef int64_t             intmax_t;
typedef uint64_t            uintmax_t;

/* Limits of exact-width integer types */
#define INT8_MIN            (-128)
#define INT16_MIN           (-32767-1)
#define INT32_MIN           (-2147483647-1)
#define INT64_MIN           (-9223372036854775807LL-1)

#define INT8_MAX            127
#define INT16_MAX           32767
#define INT32_MAX           2147483647
#define INT64_MAX           9223372036854775807LL

#define UINT8_MAX           255
#define UINT16_MAX          65535
#define UINT32_MAX          4294967295U
#define UINT64_MAX          18446744073709551615ULL

/* Limits of other integer types */
#define SIZE_MAX            UINT64_MAX
#define PTRDIFF_MIN         INT64_MIN
#define PTRDIFF_MAX         INT64_MAX

/* Macros for integer constant expressions */
#define INT8_C(x)           (x)
#define INT16_C(x)          (x)
#define INT32_C(x)          (x)
#define INT64_C(x)          (x ## LL)

#define UINT8_C(x)          (x)
#define UINT16_C(x)         (x)
#define UINT32_C(x)         (x ## U)
#define UINT64_C(x)         (x ## ULL)

#define INTMAX_C(x)         INT64_C(x)
#define UINTMAX_C(x)        UINT64_C(x)

#endif /* _STDINT_H */
