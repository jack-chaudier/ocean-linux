/*
 * Ocean Kernel - Printf Implementation
 *
 * Kernel printf with format string support.
 * Outputs to serial console by default.
 */

#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/spinlock.h>

/* External functions */
extern void serial_putc(char c);
extern size_t strlen(const char *s);

/* Printf lock for SMP safety */
static spinlock_t printf_lock = SPINLOCK_INIT;

/* Output function pointer (can be changed) */
static void (*putc_fn)(char c) = serial_putc;

/* Buffer for number formatting */
#define PRINTF_NTOA_BUFFER_SIZE 32

/* Flags for printf format parsing */
#define FLAGS_ZEROPAD   (1U << 0)
#define FLAGS_LEFT      (1U << 1)
#define FLAGS_PLUS      (1U << 2)
#define FLAGS_SPACE     (1U << 3)
#define FLAGS_HASH      (1U << 4)
#define FLAGS_UPPERCASE (1U << 5)
#define FLAGS_LONG      (1U << 6)
#define FLAGS_LONG_LONG (1U << 7)
#define FLAGS_PRECISION (1U << 8)

/* Internal output character */
static void out_char(char c)
{
    if (putc_fn) {
        putc_fn(c);
    }
}

/* Output a string */
static void out_string(const char *s, size_t len)
{
    while (len--) {
        out_char(*s++);
    }
}

/* Output padding characters */
static void out_pad(char c, int count)
{
    while (count-- > 0) {
        out_char(c);
    }
}

/* Convert unsigned integer to string */
static size_t ntoa(char *buf, u64 value, u64 base, unsigned int flags)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = (flags & FLAGS_UPPERCASE) ? digits_upper : digits_lower;
    size_t len = 0;

    if (value == 0) {
        buf[len++] = '0';
    } else {
        while (value > 0) {
            buf[len++] = digits[value % base];
            value /= base;
        }
    }

    /* Reverse the string */
    for (size_t i = 0; i < len / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = tmp;
    }

    return len;
}

/* Format and output an integer */
static void format_int(i64 value, u64 base, int width, int precision,
                       unsigned int flags)
{
    char buf[PRINTF_NTOA_BUFFER_SIZE];
    size_t len;
    bool negative = false;

    /* Handle negative numbers */
    u64 uvalue;
    if (value < 0 && base == 10) {
        negative = true;
        uvalue = (u64)(-value);
    } else {
        uvalue = (u64)value;
    }

    /* Convert to string */
    len = ntoa(buf, uvalue, base, flags);

    /* Determine padding requirements */
    int pad_zeros = 0;
    int pad_spaces = 0;

    /* Precision determines minimum digits */
    if ((flags & FLAGS_PRECISION) && precision > (int)len) {
        pad_zeros = precision - len;
    }

    /* Width determines total field width */
    int total_len = len + pad_zeros;
    if (negative || (flags & FLAGS_PLUS) || (flags & FLAGS_SPACE)) {
        total_len++;
    }
    if ((flags & FLAGS_HASH) && base == 16 && uvalue != 0) {
        total_len += 2;  /* 0x prefix */
    }
    if ((flags & FLAGS_HASH) && base == 8 && buf[0] != '0') {
        total_len++;  /* 0 prefix */
    }

    if (width > total_len) {
        if (flags & FLAGS_ZEROPAD) {
            pad_zeros += width - total_len;
        } else {
            pad_spaces = width - total_len;
        }
    }

    /* Output: left padding (spaces) */
    if (!(flags & FLAGS_LEFT) && pad_spaces > 0) {
        out_pad(' ', pad_spaces);
    }

    /* Output: sign */
    if (negative) {
        out_char('-');
    } else if (flags & FLAGS_PLUS) {
        out_char('+');
    } else if (flags & FLAGS_SPACE) {
        out_char(' ');
    }

    /* Output: base prefix */
    if ((flags & FLAGS_HASH) && base == 16 && uvalue != 0) {
        out_char('0');
        out_char((flags & FLAGS_UPPERCASE) ? 'X' : 'x');
    }
    if ((flags & FLAGS_HASH) && base == 8 && buf[0] != '0') {
        out_char('0');
    }

    /* Output: zero padding */
    out_pad('0', pad_zeros);

    /* Output: digits */
    out_string(buf, len);

    /* Output: right padding (spaces) */
    if ((flags & FLAGS_LEFT) && pad_spaces > 0) {
        out_pad(' ', pad_spaces);
    }
}

/* Format and output a string */
static void format_string(const char *s, int width, int precision,
                          unsigned int flags)
{
    if (s == NULL) {
        s = "(null)";
    }

    size_t len = strlen(s);
    if ((flags & FLAGS_PRECISION) && precision >= 0 && (size_t)precision < len) {
        len = precision;
    }

    int pad = 0;
    if (width > (int)len) {
        pad = width - len;
    }

    /* Left padding */
    if (!(flags & FLAGS_LEFT) && pad > 0) {
        out_pad(' ', pad);
    }

    /* String content */
    out_string(s, len);

    /* Right padding */
    if ((flags & FLAGS_LEFT) && pad > 0) {
        out_pad(' ', pad);
    }
}

/* Core vprintf implementation */
int kvprintf(const char *fmt, va_list ap)
{
    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            out_char(*fmt);
            fmt++;
            count++;
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Handle %% */
        if (*fmt == '%') {
            out_char('%');
            fmt++;
            count++;
            continue;
        }

        /* Parse flags */
        unsigned int flags = 0;
        bool done = false;
        while (!done) {
            switch (*fmt) {
            case '0': flags |= FLAGS_ZEROPAD; fmt++; break;
            case '-': flags |= FLAGS_LEFT; fmt++; break;
            case '+': flags |= FLAGS_PLUS; fmt++; break;
            case ' ': flags |= FLAGS_SPACE; fmt++; break;
            case '#': flags |= FLAGS_HASH; fmt++; break;
            default: done = true; break;
            }
        }

        /* Parse width */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= FLAGS_LEFT;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* Parse precision */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            flags |= FLAGS_PRECISION;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        /* Parse length modifier */
        switch (*fmt) {
        case 'l':
            fmt++;
            if (*fmt == 'l') {
                flags |= FLAGS_LONG_LONG;
                fmt++;
            } else {
                flags |= FLAGS_LONG;
            }
            break;
        case 'h':
            fmt++;
            if (*fmt == 'h') {
                fmt++;  /* hh - char */
            }
            /* short is promoted to int anyway */
            break;
        case 'z':
        case 't':
            flags |= FLAGS_LONG_LONG;  /* size_t, ptrdiff_t are 64-bit */
            fmt++;
            break;
        }

        /* Parse conversion specifier */
        switch (*fmt) {
        case 'd':
        case 'i': {
            i64 value;
            if (flags & FLAGS_LONG_LONG) {
                value = va_arg(ap, i64);
            } else if (flags & FLAGS_LONG) {
                value = va_arg(ap, long);
            } else {
                value = va_arg(ap, int);
            }
            format_int(value, 10, width, precision, flags);
            break;
        }

        case 'u': {
            u64 value;
            if (flags & FLAGS_LONG_LONG) {
                value = va_arg(ap, u64);
            } else if (flags & FLAGS_LONG) {
                value = va_arg(ap, unsigned long);
            } else {
                value = va_arg(ap, unsigned int);
            }
            format_int((i64)value, 10, width, precision, flags);
            break;
        }

        case 'x':
        case 'X': {
            u64 value;
            if (*fmt == 'X') {
                flags |= FLAGS_UPPERCASE;
            }
            if (flags & FLAGS_LONG_LONG) {
                value = va_arg(ap, u64);
            } else if (flags & FLAGS_LONG) {
                value = va_arg(ap, unsigned long);
            } else {
                value = va_arg(ap, unsigned int);
            }
            format_int((i64)value, 16, width, precision, flags);
            break;
        }

        case 'o': {
            u64 value;
            if (flags & FLAGS_LONG_LONG) {
                value = va_arg(ap, u64);
            } else if (flags & FLAGS_LONG) {
                value = va_arg(ap, unsigned long);
            } else {
                value = va_arg(ap, unsigned int);
            }
            format_int((i64)value, 8, width, precision, flags);
            break;
        }

        case 'p': {
            void *ptr = va_arg(ap, void *);
            flags |= FLAGS_HASH;
            format_int((i64)(uintptr_t)ptr, 16, width, precision, flags);
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            format_string(s, width, precision, flags);
            break;
        }

        case 'c': {
            char c = (char)va_arg(ap, int);
            if (width > 1 && !(flags & FLAGS_LEFT)) {
                out_pad(' ', width - 1);
            }
            out_char(c);
            if (width > 1 && (flags & FLAGS_LEFT)) {
                out_pad(' ', width - 1);
            }
            break;
        }

        case 'n': {
            int *n = va_arg(ap, int *);
            if (n) {
                *n = count;
            }
            break;
        }

        default:
            /* Unknown format, output as-is */
            out_char('%');
            out_char(*fmt);
            break;
        }

        fmt++;
    }

    return count;
}

/* Public printf function */
int kprintf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    spin_lock(&printf_lock);

    va_start(ap, fmt);
    ret = kvprintf(fmt, ap);
    va_end(ap);

    spin_unlock(&printf_lock);

    return ret;
}

/* Printf without lock (for use in panic) */
int kprintf_unlocked(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = kvprintf(fmt, ap);
    va_end(ap);

    return ret;
}

/* Set the output function */
void kprintf_set_output(void (*fn)(char c))
{
    putc_fn = fn;
}

/* snprintf implementation */
static char *snprintf_buf;
static size_t snprintf_pos;
static size_t snprintf_size;

static void snprintf_putc(char c)
{
    if (snprintf_pos < snprintf_size - 1) {
        snprintf_buf[snprintf_pos++] = c;
    }
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;
    void (*old_fn)(char c) = putc_fn;

    if (size == 0) {
        return 0;
    }

    spin_lock(&printf_lock);

    snprintf_buf = buf;
    snprintf_pos = 0;
    snprintf_size = size;
    putc_fn = snprintf_putc;

    va_start(ap, fmt);
    ret = kvprintf(fmt, ap);
    va_end(ap);

    buf[snprintf_pos] = '\0';
    putc_fn = old_fn;

    spin_unlock(&printf_lock);

    return ret;
}

int kvsprintf(char *buf, const char *fmt, va_list ap)
{
    void (*old_fn)(char c) = putc_fn;
    int ret;

    snprintf_buf = buf;
    snprintf_pos = 0;
    snprintf_size = SIZE_MAX;
    putc_fn = snprintf_putc;

    ret = kvprintf(fmt, ap);

    buf[snprintf_pos] = '\0';
    putc_fn = old_fn;

    return ret;
}
