/*
 * echo - Print arguments to standard output
 *
 * Simple implementation of the echo utility for Ocean.
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int newline = 1;
    int start = 1;

    /* Check for -n flag */
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        start = 2;
    }

    /* Print arguments separated by spaces */
    for (int i = start; i < argc; i++) {
        if (i > start) {
            putchar(' ');
        }
        printf("%s", argv[i]);
    }

    if (newline) {
        putchar('\n');
    }

    return 0;
}
