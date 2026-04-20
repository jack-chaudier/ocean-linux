#ifndef _OCEAN_USERSPACE_MANIFEST_H
#define _OCEAN_USERSPACE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ocean/ipc_proto.h>

struct ocean_boot_module_spec {
    const char *name;
    const char *path;
    const char *summary;
    int runnable_from_shell;
};

struct ocean_service_spec {
    const char *name;
    const char *path;
    const char *summary;
    uint32_t well_known_ep;
    int priority;
};

static const struct ocean_boot_module_spec ocean_boot_module_specs[] = {
    {
        .name = "init",
        .path = "/boot/init.elf",
        .summary = "Initial userspace service manager",
        .runnable_from_shell = 0,
    },
    {
        .name = "sh",
        .path = "/boot/sh.elf",
        .summary = "Interactive shell",
        .runnable_from_shell = 1,
    },
    {
        .name = "echo",
        .path = "/boot/echo.elf",
        .summary = "Print arguments",
        .runnable_from_shell = 1,
    },
    {
        .name = "cat",
        .path = "/boot/cat.elf",
        .summary = "Concatenate files or standard input",
        .runnable_from_shell = 1,
    },
    {
        .name = "ls",
        .path = "/boot/ls.elf",
        .summary = "List the read-only boot directory",
        .runnable_from_shell = 1,
    },
};

static const struct ocean_service_spec ocean_service_specs[] = {
    {
        .name = "mem",
        .path = "/boot/mem.elf",
        .summary = "Memory policy server",
        .well_known_ep = EP_MEM,
        .priority = 0,
    },
    {
        .name = "proc",
        .path = "/boot/proc.elf",
        .summary = "Process lifecycle server",
        .well_known_ep = EP_PROC,
        .priority = 1,
    },
    {
        .name = "blk",
        .path = "/boot/blk.elf",
        .summary = "Block device multiplexer",
        .well_known_ep = EP_BLK,
        .priority = 2,
    },
    {
        .name = "ata",
        .path = "/boot/ata.elf",
        .summary = "ATA disk driver",
        .well_known_ep = 0,
        .priority = 3,
    },
    {
        .name = "vfs",
        .path = "/boot/vfs.elf",
        .summary = "Virtual filesystem server",
        .well_known_ep = EP_VFS,
        .priority = 4,
    },
    {
        .name = "ramfs",
        .path = "/boot/ramfs.elf",
        .summary = "In-memory bootstrap filesystem",
        .well_known_ep = 0,
        .priority = 4,
    },
    {
        .name = "ext2",
        .path = "/boot/ext2.elf",
        .summary = "Read-only ext2 filesystem driver",
        .well_known_ep = 0,
        .priority = 4,
    },
};

#define OCEAN_BOOT_MODULE_SPEC_COUNT \
    (sizeof(ocean_boot_module_specs) / sizeof(ocean_boot_module_specs[0]))

#define OCEAN_SERVICE_SPEC_COUNT \
    (sizeof(ocean_service_specs) / sizeof(ocean_service_specs[0]))

static inline const struct ocean_boot_module_spec *
ocean_find_boot_module_spec(const char *name)
{
    if (!name || !*name) {
        return NULL;
    }

    for (size_t i = 0; i < OCEAN_BOOT_MODULE_SPEC_COUNT; i++) {
        if (strcmp(ocean_boot_module_specs[i].name, name) == 0 ||
            strcmp(ocean_boot_module_specs[i].path, name) == 0) {
            return &ocean_boot_module_specs[i];
        }
    }

    return NULL;
}

static inline const struct ocean_service_spec *
ocean_find_service_spec(const char *name)
{
    if (!name || !*name) {
        return NULL;
    }

    for (size_t i = 0; i < OCEAN_SERVICE_SPEC_COUNT; i++) {
        if (strcmp(ocean_service_specs[i].name, name) == 0 ||
            strcmp(ocean_service_specs[i].path, name) == 0) {
            return &ocean_service_specs[i];
        }
    }

    return NULL;
}

#endif /* _OCEAN_USERSPACE_MANIFEST_H */
