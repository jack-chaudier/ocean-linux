/*
 * cat - Concatenate and print files
 *
 * Ocean currently supports stdin plus read-only boot-module files.
 */

#include <stdio.h>
#include <string.h>

#include <ocean/syscall.h>
#include <ocean/userspace_manifest.h>

static void print_usage(void)
{
    printf("usage: cat [--help] [FILE...|-]\n");
    printf("  -   read from standard input\n");
}

static int stream_fd(int fd, const char *label)
{
    char buf[256];

    while (1) {
        int64_t n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (label) {
                printf("cat: read failed: %s\n", label);
            } else {
                printf("cat: read failed\n");
            }
            return 1;
        }

        if (write(1, buf, (uint64_t)n) != n) {
            printf("cat: write failed\n");
            return 1;
        }
    }
}

int main(int argc, char **argv)
{
    int exit_code = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
    }

    if (argc <= 1) {
        return stream_fd(0, NULL);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (stream_fd(0, NULL) != 0) {
                exit_code = 1;
            }
            continue;
        }

        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            const struct ocean_boot_module_spec *module =
                ocean_find_boot_module_spec(argv[i]);

            printf("cat: cannot open %s\n", argv[i]);
            if (module) {
                printf("cat: try the boot-module path %s\n", module->path);
            }
            exit_code = 1;
            continue;
        }

        if (stream_fd(fd, argv[i]) != 0) {
            exit_code = 1;
        }
        close(fd);
    }

    return exit_code;
}
