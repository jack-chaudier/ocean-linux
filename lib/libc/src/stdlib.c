/*
 * Ocean libc - Standard Library Implementation
 */

#include <stdlib.h>
#include <string.h>
#include <ocean/syscall.h>

#define LIBC_HEAP_SIZE  (128 * 1024)
#define LIBC_HEAP_ALIGN 16

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static unsigned char heap_area[LIBC_HEAP_SIZE] __attribute__((aligned(LIBC_HEAP_ALIGN)));
static struct heap_block *heap_head;

static size_t align_up_size(size_t value, size_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static void heap_init(void)
{
    if (heap_head) {
        return;
    }

    heap_head = (struct heap_block *)heap_area;
    heap_head->size = LIBC_HEAP_SIZE - sizeof(*heap_head);
    heap_head->free = 1;
    heap_head->next = NULL;
}

static void split_block(struct heap_block *block, size_t size)
{
    unsigned char *raw = (unsigned char *)(block + 1);
    struct heap_block *next = (struct heap_block *)(raw + size);

    next->size = block->size - size - sizeof(*next);
    next->free = 1;
    next->next = block->next;

    block->size = size;
    block->next = next;
}

static void coalesce_blocks(void)
{
    struct heap_block *block = heap_head;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(*block) + block->next->size;
            block->next = block->next->next;
            continue;
        }
        block = block->next;
    }
}

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

void *malloc(size_t size)
{
    struct heap_block *block;
    size_t aligned_size;

    if (size == 0) {
        return NULL;
    }

    heap_init();
    aligned_size = align_up_size(size, LIBC_HEAP_ALIGN);

    for (block = heap_head; block; block = block->next) {
        if (!block->free || block->size < aligned_size) {
            continue;
        }

        if (block->size >= aligned_size + sizeof(*block) + LIBC_HEAP_ALIGN) {
            split_block(block, aligned_size);
        }

        block->free = 0;
        return (void *)(block + 1);
    }

    return NULL;
}

void free(void *ptr)
{
    struct heap_block *block;

    if (!ptr) {
        return;
    }

    block = ((struct heap_block *)ptr) - 1;
    block->free = 1;
    coalesce_blocks();
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total;
    void *ptr;

    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    if (nmemb > ((size_t)-1) / size) {
        return NULL;
    }

    total = nmemb * size;
    ptr = malloc(total);
    if (!ptr) {
        return NULL;
    }

    memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    struct heap_block *block;
    void *new_ptr;
    size_t aligned_size;

    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block = ((struct heap_block *)ptr) - 1;
    aligned_size = align_up_size(size, LIBC_HEAP_ALIGN);

    if (block->size >= aligned_size) {
        if (block->size >= aligned_size + sizeof(*block) + LIBC_HEAP_ALIGN) {
            split_block(block, aligned_size);
        }
        return ptr;
    }

    if (block->next && block->next->free &&
        block->size + sizeof(*block) + block->next->size >= aligned_size) {
        block->size += sizeof(*block) + block->next->size;
        block->next = block->next->next;
        if (block->size >= aligned_size + sizeof(*block) + LIBC_HEAP_ALIGN) {
            split_block(block, aligned_size);
        }
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size < size ? block->size : size);
    free(ptr);
    return new_ptr;
}
