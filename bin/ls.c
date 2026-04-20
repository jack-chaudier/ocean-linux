/*
 * ls - List directory contents
 *
 * Simple implementation of ls for Ocean.
 * Without a live VFS, it exposes the boot modules that are currently loaded.
 */

#include <stdio.h>
#include <string.h>

#include <ocean/syscall.h>
#include <ocean/userspace_manifest.h>

static void print_usage(void)
{
    printf("usage: ls [--help] [/boot]\n");
}

int main(int argc, char **argv)
{
    const char *target = "/boot";

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }

        if (strcmp(argv[1], "/boot") != 0) {
            printf("ls: only /boot is available until VFS is wired up\n");
            return 1;
        }

        target = argv[1];
    }

    printf("%s\n", target);
    printf("  NAME   SIZE      RUN  SUMMARY\n");
    printf("  -----  --------  ---  ------------------------------\n");

    for (size_t i = 0; i < OCEAN_BOOT_MODULE_SPEC_COUNT; i++) {
        const struct ocean_boot_module_spec *module = &ocean_boot_module_specs[i];
        int fd = open(module->path, O_RDONLY, 0);
        int64_t size = -1;

        if (fd >= 0) {
            size = lseek(fd, 0, SEEK_END);
            close(fd);
        }

        printf("  %-5s  %-8lld  %-3s  %s\n",
               module->name,
               (long long)size,
               module->runnable_from_shell ? "yes" : "no",
               module->summary);
    }

    return 0;
}
