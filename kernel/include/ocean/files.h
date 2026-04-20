#ifndef _OCEAN_FILES_H
#define _OCEAN_FILES_H

#include <ocean/boot.h>
#include <ocean/types.h>

struct process;

#define PROCESS_MAX_OPEN_FILES 16
#define PROCESS_FD_STDIN       0
#define PROCESS_FD_STDOUT      1
#define PROCESS_FD_STDERR      2
#define PROCESS_FD_FIRST_USER  3

enum process_file_kind {
    PROCESS_FILE_NONE = 0,
    PROCESS_FILE_BOOT_MODULE = 1,
};

struct process_file {
    u32 kind;
    u32 flags;
    const struct cached_module *module;
    u64 offset;
};

struct process_files {
    struct process_file entries[PROCESS_MAX_OPEN_FILES];
};

struct process_files *process_files_create(void);
struct process_files *process_files_clone(const struct process_files *src);
void process_files_destroy(struct process_files *files);

#endif /* _OCEAN_FILES_H */
