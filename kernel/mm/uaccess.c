/*
 * Ocean Kernel - User Memory Access Helpers
 *
 * Centralized user pointer validation and copy helpers.
 */

#include <ocean/uaccess.h>
#include <ocean/process.h>
#include <ocean/vmm.h>
#include <ocean/defs.h>

/* External functions */
extern void *memcpy(void *dest, const void *src, size_t n);

int validate_user_range(const void *ptr, size_t len, u32 required_vma_flags)
{
    if (!ptr) {
        return -EFAULT;
    }
    if (len == 0) {
        return 0;
    }

    u64 start = (u64)(uintptr_t)ptr;
    if (start > USER_SPACE_END) {
        return -EFAULT;
    }

    if ((len - 1) > (USER_SPACE_END - start)) {
        return -EFAULT;
    }

    u64 end = start + len;

    struct process *proc = get_current_process();
    if (!proc || !proc->mm) {
        return -EFAULT;
    }

    struct address_space *as = proc->mm;
    u64 flags;
    spin_lock_irqsave(&as->lock, &flags);

    u64 cursor = start;
    while (cursor < end) {
        struct vm_area *vma = vmm_find_vma(as, cursor);
        if (!vma || cursor < vma->start) {
            spin_unlock_irqrestore(&as->lock, flags);
            return -EFAULT;
        }

        if ((required_vma_flags & VMA_READ) && !(vma->flags & VMA_READ)) {
            spin_unlock_irqrestore(&as->lock, flags);
            return -EFAULT;
        }
        if ((required_vma_flags & VMA_WRITE) && !(vma->flags & VMA_WRITE)) {
            spin_unlock_irqrestore(&as->lock, flags);
            return -EFAULT;
        }
        if ((required_vma_flags & VMA_EXEC) && !(vma->flags & VMA_EXEC)) {
            spin_unlock_irqrestore(&as->lock, flags);
            return -EFAULT;
        }

        if (vma->end <= cursor) {
            spin_unlock_irqrestore(&as->lock, flags);
            return -EFAULT;
        }

        if (vma->end >= end) {
            break;
        }

        cursor = vma->end;
    }

    spin_unlock_irqrestore(&as->lock, flags);
    return 0;
}

int copy_from_user(void *dst, const void *src, size_t len)
{
    if (!dst) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    int ret = validate_user_range(src, len, VMA_READ);
    if (ret < 0) {
        return ret;
    }

    memcpy(dst, src, len);
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t len)
{
    if (!src) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    int ret = validate_user_range(dst, len, VMA_WRITE);
    if (ret < 0) {
        return ret;
    }

    memcpy(dst, src, len);
    return 0;
}

int copy_string_from_user(char *dst, size_t dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < dst_size; i++) {
        int ret = validate_user_range((const void *)(src + i), 1, VMA_READ);
        if (ret < 0) {
            return ret;
        }

        dst[i] = *((volatile const char *)(src + i));
        if (dst[i] == '\0') {
            return (int)i;
        }
    }

    dst[dst_size - 1] = '\0';
    return -ENAMETOOLONG;
}
