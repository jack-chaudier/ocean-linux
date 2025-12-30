/*
 * Ocean libc - unistd.h
 *
 * Standard POSIX symbolic constants and types.
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

/* POSIX-like types */
typedef long ssize_t;
typedef int pid_t;

/* File descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* I/O functions */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

/* Process functions */
pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
void _exit(int status) __attribute__((noreturn));

/* Misc */
int usleep(unsigned int usec);

#endif /* _UNISTD_H */
