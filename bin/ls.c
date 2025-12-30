/*
 * ls - List directory contents
 *
 * Simple implementation of ls for Ocean.
 * Currently only shows boot modules since VFS is not yet available.
 */

#include <stdio.h>

/* Boot module information (would come from kernel) */
/* For now, just display a placeholder message */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("ls: VFS not yet available\n");
    printf("Boot modules:\n");
    printf("  /boot/init.elf\n");
    printf("  /boot/sh.elf\n");
    printf("  /boot/echo.elf\n");
    printf("  /boot/cat.elf\n");
    printf("  /boot/ls.elf\n");

    return 0;
}
