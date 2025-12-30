/*
 * Ocean Shell
 *
 * A simple interactive shell for the Ocean microkernel.
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>

#define SHELL_VERSION "0.1.0"
#define MAX_LINE 256
#define MAX_ARGS 16
#define PROMPT "ocean$ "

/* Command buffer */
static char line[MAX_LINE];
static char *argv[MAX_ARGS];
static int argc;

/*
 * Read a line of input
 */
static int read_line(void)
{
    int i = 0;
    char c;

    while (i < MAX_LINE - 1) {
        if (read(0, &c, 1) != 1) {
            return -1;
        }

        if (c == '\n') {
            line[i] = '\0';
            return i;
        }

        /* Handle backspace */
        if (c == 0x7F || c == '\b') {
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }

        line[i++] = c;
    }

    line[i] = '\0';
    return i;
}

/*
 * Parse command line into argc/argv
 */
static void parse_line(void)
{
    argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }

        if (*p) {
            *p++ = '\0';
        }
    }

    argv[argc] = NULL;
}

/*
 * Built-in commands
 */

static void cmd_help(void)
{
    printf("Ocean Shell v%s\n", SHELL_VERSION);
    printf("\nBuilt-in commands:\n");
    printf("  help          Show this help\n");
    printf("  exit          Exit the shell\n");
    printf("  echo [args]   Print arguments\n");
    printf("  pid           Show current process ID\n");
    printf("  clear         Clear screen\n");
    printf("\nOther commands are executed from boot modules.\n");
}

static void cmd_echo(void)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void cmd_pid(void)
{
    printf("PID: %d, PPID: %d\n", getpid(), getppid());
}

static void cmd_clear(void)
{
    /* ANSI escape sequence to clear screen */
    printf("\033[2J\033[H");
}

/*
 * Execute external command
 */
static void exec_external(void)
{
    /* Build path for boot module lookup */
    char path[64];
    if (argv[0][0] != '/') {
        /* Try /boot/<cmd>.elf */
        snprintf(path, sizeof(path), "/boot/%s.elf", argv[0]);
    } else {
        strncpy(path, argv[0], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    /* Fork and exec */
    int pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        return;
    }

    if (pid == 0) {
        /* Child process */
        exec(path, argv, NULL);
        /* If exec returns, it failed */
        printf("%s: command not found\n", argv[0]);
        _exit(1);
    } else {
        /* Parent waits for child */
        int status;
        wait(&status);
    }
}

/*
 * Execute command
 */
static void execute(void)
{
    if (argc == 0) {
        return;
    }

    /* Check built-in commands */
    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        printf("Goodbye!\n");
        _exit(0);
    } else if (strcmp(argv[0], "echo") == 0) {
        cmd_echo();
    } else if (strcmp(argv[0], "pid") == 0) {
        cmd_pid();
    } else if (strcmp(argv[0], "clear") == 0) {
        cmd_clear();
    } else {
        /* Try external command */
        exec_external();
    }
}

/*
 * Print welcome banner
 */
static void print_banner(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  Ocean Shell v%s\n", SHELL_VERSION);
    printf("  Type 'help' for available commands\n");
    printf("========================================\n");
    printf("\n");
}

/*
 * Main entry point
 */
int main(int argc_unused, char **argv_unused)
{
    (void)argc_unused;
    (void)argv_unused;

    print_banner();

    /* Main shell loop */
    while (1) {
        printf("%s", PROMPT);

        if (read_line() < 0) {
            printf("\nEOF\n");
            break;
        }

        parse_line();
        execute();
    }

    return 0;
}
