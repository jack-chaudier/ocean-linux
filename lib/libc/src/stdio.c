/*
 * Ocean libc - Standard I/O Implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ocean/syscall.h>

/*
 * Output a single character to stdout
 */
int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1) {
        return EOF;
    }
    return c;
}

/*
 * Output a string followed by newline
 */
int puts(const char *s)
{
    size_t len = strlen(s);
    if (write(STDOUT_FILENO, s, len) != (ssize_t)len) {
        return EOF;
    }
    if (putchar('\n') == EOF) {
        return EOF;
    }
    return 0;
}

/*
 * Simple number to string conversion
 */
static int format_number(char *buf, size_t size, uint64_t value,
                        int base, int is_signed, int width, char pad)
{
    char tmp[24];
    char *p = tmp + sizeof(tmp);
    int negative = 0;
    int len;

    if (is_signed && (int64_t)value < 0) {
        negative = 1;
        value = -(int64_t)value;
    }

    *--p = '\0';

    if (value == 0) {
        *--p = '0';
    } else {
        while (value) {
            int digit = value % base;
            *--p = (digit < 10) ? '0' + digit : 'a' + digit - 10;
            value /= base;
        }
    }

    if (negative) {
        *--p = '-';
    }

    len = tmp + sizeof(tmp) - 1 - p;

    /* Pad if needed */
    while (width > len && size > 1) {
        *buf++ = pad;
        size--;
        width--;
    }

    /* Copy number */
    while (*p && size > 1) {
        *buf++ = *p++;
        size--;
    }

    *buf = '\0';
    return len > width ? len : width;
}

/*
 * vsnprintf - formatted output to sized buffer
 */
int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    char *out = str;
    char *end = str + size - 1;
    const char *p;

    if (size == 0) {
        return 0;
    }

    for (p = format; *p && out < end; p++) {
        if (*p != '%') {
            *out++ = *p;
            continue;
        }

        p++;

        /* Handle flags */
        char pad = ' ';
        int width = 0;
        int left_justify = 0;

        if (*p == '-') {
            left_justify = 1;
            p++;
        }
        if (*p == '0' && !left_justify) {
            pad = '0';
            p++;
        }

        /* Handle width */
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        /* Handle length modifiers */
        int is_long = 0;
        int is_longlong = 0;

        if (*p == 'l') {
            is_long = 1;
            p++;
            if (*p == 'l') {
                is_longlong = 1;
                p++;
            }
        }

        /* Handle format specifier */
        switch (*p) {
        case '%':
            *out++ = '%';
            break;

        case 'c':
            *out++ = (char)va_arg(ap, int);
            break;

        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = 0;
            const char *sp = s;
            while (*sp) { slen++; sp++; }

            /* Right-pad first if right-justified */
            if (!left_justify) {
                while (slen < width && out < end) {
                    *out++ = ' ';
                    width--;
                }
            }

            /* Copy string */
            while (*s && out < end) {
                *out++ = *s++;
            }

            /* Left-pad if left-justified */
            if (left_justify) {
                while (slen < width && out < end) {
                    *out++ = ' ';
                    slen++;
                }
            }
            break;
        }

        case 'd':
        case 'i': {
            int64_t val;
            if (is_longlong) {
                val = va_arg(ap, int64_t);
            } else if (is_long) {
                val = va_arg(ap, long);
            } else {
                val = va_arg(ap, int);
            }
            char tmp[24];
            int len = format_number(tmp, sizeof(tmp), val, 10, 1,
                                    left_justify ? 0 : width, pad);

            /* Right-pad first if right-justified (already handled by format_number) */
            /* Copy number */
            for (int i = 0; tmp[i] && out < end; i++) {
                *out++ = tmp[i];
            }

            /* Left-pad if left-justified */
            if (left_justify) {
                while (len < width && out < end) {
                    *out++ = ' ';
                    len++;
                }
            }
            break;
        }

        case 'u': {
            uint64_t val;
            if (is_longlong) {
                val = va_arg(ap, uint64_t);
            } else if (is_long) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            char tmp[24];
            int len = format_number(tmp, sizeof(tmp), val, 10, 0, width, pad);
            for (int i = 0; tmp[i] && out < end; i++) {
                *out++ = tmp[i];
            }
            (void)len;
            break;
        }

        case 'x':
        case 'X': {
            uint64_t val;
            if (is_longlong) {
                val = va_arg(ap, uint64_t);
            } else if (is_long) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            char tmp[24];
            int len = format_number(tmp, sizeof(tmp), val, 16, 0, width, pad);
            for (int i = 0; tmp[i] && out < end; i++) {
                *out++ = tmp[i];
            }
            (void)len;
            break;
        }

        case 'p': {
            void *ptr = va_arg(ap, void *);
            if (out + 2 < end) {
                *out++ = '0';
                *out++ = 'x';
            }
            char tmp[24];
            format_number(tmp, sizeof(tmp), (uint64_t)ptr, 16, 0, 16, '0');
            for (int i = 0; tmp[i] && out < end; i++) {
                *out++ = tmp[i];
            }
            break;
        }

        default:
            /* Unknown format, just output as-is */
            if (out < end) *out++ = '%';
            if (out < end) *out++ = *p;
            break;
        }
    }

    *out = '\0';
    return out - str;
}

int vsprintf(char *str, const char *format, va_list ap)
{
    return vsnprintf(str, SIZE_MAX, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap)
{
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        write(STDOUT_FILENO, buf, len);
    }
    return len;
}

int printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}
