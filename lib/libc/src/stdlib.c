/*
 * Ocean libc - Standard Library Implementation
 */

#include <stdlib.h>
#include <ocean/syscall.h>

/*
 * Exit the process
 */
void exit(int status)
{
    _exit(status);
    /* _exit never returns, but just in case */
    __builtin_unreachable();
}

void abort(void)
{
    /* TODO: Send SIGABRT */
    _exit(134);  /* 128 + SIGABRT(6) */
    __builtin_unreachable();
}

int abs(int j)
{
    return j < 0 ? -j : j;
}

long labs(long j)
{
    return j < 0 ? -j : j;
}

/*
 * String to integer conversion
 */
static int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

static int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int atoi(const char *nptr)
{
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char *nptr)
{
    return strtol(nptr, NULL, 10);
}

long long atoll(const char *nptr)
{
    return (long long)strtol(nptr, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    long result = 0;
    int negative = 0;

    /* Skip whitespace */
    while (isspace(*s)) {
        s++;
    }

    /* Handle sign */
    if (*s == '-') {
        negative = 1;
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

        if (isdigit(*s)) {
            digit = *s - '0';
        } else if (isalpha(*s)) {
            digit = (*s | 0x20) - 'a' + 10;
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
    /* Simple implementation - just cast */
    return (unsigned long)strtol(nptr, endptr, base);
}

/*
 * Memory allocation - stub implementations
 * TODO: Implement proper heap allocator
 */
void *malloc(size_t size)
{
    (void)size;
    return NULL;
}

void free(void *ptr)
{
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    (void)nmemb;
    (void)size;
    return NULL;
}

void *realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return NULL;
}
