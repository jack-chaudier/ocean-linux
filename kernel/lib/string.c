/*
 * Ocean Kernel - String Functions
 *
 * Basic string and memory manipulation functions for the kernel.
 * Freestanding implementation (no libc dependency).
 */

#include <ocean/types.h>
#include <ocean/defs.h>

/*
 * Memory functions
 */

void *memset(void *s, int c, size_t n)
{
    u8 *p = (u8 *)s;
    u8 val = (u8)c;

    /* Optimize for common case of zeroing */
    if (val == 0 && n >= 8 && IS_ALIGNED((uintptr_t)p, 8)) {
        /* Zero using 64-bit stores */
        u64 *p64 = (u64 *)p;
        while (n >= 8) {
            *p64++ = 0;
            n -= 8;
        }
        p = (u8 *)p64;
    }

    /* Handle remaining bytes */
    while (n--) {
        *p++ = val;
    }

    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    /* Optimize for aligned copies */
    if (IS_ALIGNED((uintptr_t)d, 8) && IS_ALIGNED((uintptr_t)s, 8)) {
        u64 *d64 = (u64 *)d;
        const u64 *s64 = (const u64 *)s;
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        d = (u8 *)d64;
        s = (const u8 *)s64;
    }

    /* Handle remaining bytes */
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    if (d == s || n == 0) {
        return dest;
    }

    /* If regions don't overlap, use memcpy */
    if (d < s || d >= s + n) {
        return memcpy(dest, src, n);
    }

    /* Copy backwards for overlapping regions where dest > src */
    d += n;
    s += n;
    while (n--) {
        *--d = *--s;
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const u8 *p1 = (const u8 *)s1;
    const u8 *p2 = (const u8 *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const u8 *p = (const u8 *)s;
    u8 val = (u8)c;

    while (n--) {
        if (*p == val) {
            return (void *)p;
        }
        p++;
    }

    return NULL;
}

/*
 * String functions
 */

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t maxlen)
{
    const char *p = s;
    while (maxlen-- && *p) {
        p++;
    }
    return (size_t)(p - s);
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;

    while (n && (*d++ = *src++)) {
        n--;
    }

    /* Pad with zeros if src was shorter than n */
    while (n--) {
        *d++ = '\0';
    }

    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d) {
        d++;
    }
    while (n-- && (*d++ = *src++))
        ;
    *d = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (int)(u8)*s1 - (int)(u8)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (int)(u8)*s1 - (int)(u8)*s2;
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (*s) {
        if (*s == ch) {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    char ch = (char)c;
    const char *last = NULL;

    while (*s) {
        if (*s == ch) {
            last = s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);

    if (needle_len == 0) {
        return (char *)haystack;
    }

    while (*haystack) {
        if (*haystack == *needle) {
            if (strncmp(haystack, needle, needle_len) == 0) {
                return (char *)haystack;
            }
        }
        haystack++;
    }

    return NULL;
}

/*
 * Number conversion
 */

/* Convert string to integer */
long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    long result = 0;
    bool negative = false;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }

    /* Handle sign */
    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }

    /* Handle base prefix */
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    /* Convert digits */
    while (*s) {
        int digit;

        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        result = result * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char *)s;
    }

    return negative ? -result : result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    return (unsigned long)strtol(nptr, endptr, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

/*
 * Character classification
 */

int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int toupper(int c)
{
    if (islower(c)) {
        return c - 'a' + 'A';
    }
    return c;
}

int tolower(int c)
{
    if (isupper(c)) {
        return c - 'A' + 'a';
    }
    return c;
}

int isprint(int c)
{
    return c >= 0x20 && c <= 0x7E;
}
