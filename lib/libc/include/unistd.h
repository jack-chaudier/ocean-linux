/*
 * Ocean libc - unistd.h
 *
 * Standard POSIX symbolic constants and types.
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <ocean/syscall.h>

/* POSIX-like types */
typedef long ssize_t;
typedef int pid_t;

/* File descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* The actual syscall wrappers live in <ocean/syscall.h>. */

#endif /* _UNISTD_H */
