/*
 * Ocean Kernel - Process File Tables
 *
 * Minimal per-process file descriptor tables for kernel-owned objects.
 */

#include <ocean/files.h>

/* External functions */
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

struct process_files *process_files_create(void)
{
    struct process_files *files = kmalloc(sizeof(*files));
    if (!files) {
        return NULL;
    }

    memset(files, 0, sizeof(*files));
    return files;
}

struct process_files *process_files_clone(const struct process_files *src)
{
    struct process_files *files = process_files_create();
    if (!files) {
        return NULL;
    }

    if (src) {
        memcpy(files, src, sizeof(*files));
    }

    return files;
}

void process_files_destroy(struct process_files *files)
{
    if (files) {
        kfree(files);
    }
}
