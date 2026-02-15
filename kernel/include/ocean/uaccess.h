/*
 * Ocean Kernel - User Memory Access Helpers
 *
 * Centralized helpers for validating and copying user pointers.
 */

#ifndef _OCEAN_UACCESS_H
#define _OCEAN_UACCESS_H

#include <ocean/types.h>

/* Forward declaration to avoid heavy includes in callers */
struct process;

/*
 * Validate that [ptr, ptr + len) is a valid user range.
 *
 * required_vma_flags is a VMA_* bitmask (for example VMA_READ/VMA_WRITE).
 * Returns 0 on success or -EFAULT/-EINVAL on failure.
 */
int validate_user_range(const void *ptr, size_t len, u32 required_vma_flags);

/* Copy from user memory into kernel memory. Returns 0 or negative errno. */
int copy_from_user(void *dst, const void *src, size_t len);

/* Copy from kernel memory into user memory. Returns 0 or negative errno. */
int copy_to_user(void *dst, const void *src, size_t len);

/*
 * Copy a NUL-terminated user string into a kernel buffer.
 * On success returns string length (excluding NUL).
 * Returns -EFAULT for invalid user memory or -ENAMETOOLONG if no NUL fits.
 */
int copy_string_from_user(char *dst, size_t dst_size, const char *src);

#endif /* _OCEAN_UACCESS_H */
