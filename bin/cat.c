/*
 * cat - Concatenate and print files
 *
 * Simple implementation of cat for Ocean.
 * Currently only supports stdin since VFS is not yet available.
 */

#include <stdio.h>
#include <ocean/syscall.h>

int main(int argc, char **argv)
{
    char buf[256];

    if (argc > 1) {
        /* File arguments - not yet supported */
        printf("cat: file arguments not yet supported\n");
        printf("cat: reading from stdin instead\n");
    }

    /* Read from stdin and echo to stdout */
    while (1) {
        int64_t n = read(0, buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        printf("%s", buf);
    }

    return 0;
}
