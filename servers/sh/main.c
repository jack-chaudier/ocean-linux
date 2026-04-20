/*
 * Ocean Shell
 *
 * A simple interactive shell for the Ocean microkernel.
 */

#include <stdio.h>
#include <string.h>

#include <ocean/syscall.h>
#include <ocean/userspace_manifest.h>

#define SHELL_VERSION "0.3.0"
#define MAX_LINE 256
#define MAX_ARGS 32
#define MAX_PATH 64

static char current_dir[MAX_PATH] = "/";

#define READ_LINE_EOF       (-1)
#define READ_LINE_TOO_LONG  (-2)

/* Command buffer */
static char line[MAX_LINE];
static char *argv[MAX_ARGS];
static int argc;

static int is_shell_space(char c)
{
    return c == ' ' || c == '\t';
}

static void print_prompt(void)
{
    printf("ocean:%s$ ", current_dir);
}

static int set_current_dir(const char *path)
{
    if (!path || !*path || strcmp(path, "~") == 0 || strcmp(path, "/") == 0) {
        snprintf(current_dir, sizeof(current_dir), "/");
        return 0;
    }

    if (strcmp(path, ".") == 0) {
        return 0;
    }

    if (strcmp(path, "..") == 0) {
        snprintf(current_dir, sizeof(current_dir), "/");
        return 0;
    }

    if (strcmp(path, "boot") == 0 || strcmp(path, "/boot") == 0) {
        snprintf(current_dir, sizeof(current_dir), "/boot");
        return 0;
    }

    return -1;
}

static int drain_line(void)
{
    char c;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            return 0;
        }
    }

    return -1;
}

/*
 * Read a line of input
 */
static int read_line(void)
{
    int i = 0;
    char c;

    while (i < MAX_LINE - 1) {
        if (read(0, &c, 1) != 1) {
            return READ_LINE_EOF;
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

    line[MAX_LINE - 1] = '\0';
    drain_line();
    return READ_LINE_TOO_LONG;
}

/*
 * Parse command line into argc/argv with simple quote support.
 */
static int parse_line(void)
{
    char *src = line;
    char *dst = line;
    char quote = '\0';

    argc = 0;

    while (*src) {
        while (is_shell_space(*src)) {
            src++;
        }

        if (*src == '\0') {
            break;
        }

        if (argc >= MAX_ARGS - 1) {
            printf("sh: too many arguments (max %d)\n", MAX_ARGS - 1);
            argc = 0;
            argv[0] = NULL;
            return -1;
        }

        argv[argc++] = dst;

        while (*src) {
            char c = *src++;

            if (quote) {
                if (c == quote) {
                    quote = '\0';
                    continue;
                }
                if (quote == '"' && c == '\\' && *src) {
                    c = *src++;
                }
                *dst++ = c;
                continue;
            }

            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }

            if (c == '\\' && *src) {
                *dst++ = *src++;
                continue;
            }

            if (is_shell_space(c)) {
                break;
            }

            *dst++ = c;
        }

        if (quote) {
            printf("sh: unterminated quote\n");
            argc = 0;
            argv[0] = NULL;
            return -1;
        }

        *dst++ = '\0';
    }

    argv[argc] = NULL;
    return 0;
}

static void print_boot_commands(void)
{
    printf("\nBoot commands:\n");
    for (size_t i = 0; i < OCEAN_BOOT_MODULE_SPEC_COUNT; i++) {
        const struct ocean_boot_module_spec *module = &ocean_boot_module_specs[i];
        if (!module->runnable_from_shell) {
            continue;
        }

        printf("  %-6s  %s\n", module->name, module->summary);
    }
}

/*
 * Built-in commands
 */

static void cmd_help(void)
{
    printf("Ocean Shell v%s\n", SHELL_VERSION);
    printf("\nBuilt-in commands:\n");
    printf("  help             Show this help\n");
    printf("  exit             Exit the shell\n");
    printf("  cd [dir]         Change between / and /boot\n");
    printf("  pwd              Print the current shell directory\n");
    printf("  echo [args]      Print arguments\n");
    printf("  pid              Show current process ID\n");
    printf("  clear            Clear the screen\n");
    printf("  modules          List loaded boot modules\n");
    printf("  services         Show the init service plan\n");
    printf("  which <name>     Resolve a boot module or service path\n");
    printf("  version          Show shell version details\n");
    print_boot_commands();
    printf("\nUse quotes to keep spaces together, for example: echo \"hello ocean\"\n");
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

static void cmd_cd(void)
{
    const char *target = argc > 1 ? argv[1] : "/";

    if (argc > 2) {
        printf("usage: cd [/|/boot]\n");
        return;
    }

    if (set_current_dir(target) < 0) {
        printf("sh: cd: only / and /boot are available until VFS is wired up\n");
    }
}

static void cmd_pwd(void)
{
    printf("%s\n", current_dir);
}

static void cmd_clear(void)
{
    /* ANSI escape sequence to clear screen */
    printf("\033[2J\033[H");
}

static void cmd_modules(void)
{
    printf("Loaded boot modules:\n");
    printf("  NAME   PATH            RUN  SUMMARY\n");
    printf("  -----  --------------  ---  ------------------------------\n");

    for (size_t i = 0; i < OCEAN_BOOT_MODULE_SPEC_COUNT; i++) {
        const struct ocean_boot_module_spec *module = &ocean_boot_module_specs[i];
        printf("  %-5s  %-14s  %-3s  %s\n",
               module->name,
               module->path,
               module->runnable_from_shell ? "yes" : "no",
               module->summary);
    }
}

static void cmd_services(void)
{
    printf("Init service plan:\n");
    printf("  PRIO  NAME   EP   SUMMARY\n");
    printf("  ----  -----  ---  ------------------------------\n");

    for (size_t i = 0; i < OCEAN_SERVICE_SPEC_COUNT; i++) {
        const struct ocean_service_spec *service = &ocean_service_specs[i];
        printf("  %-4d  %-5s  %-3u  %s\n",
               service->priority,
               service->name,
               service->well_known_ep,
               service->summary);
    }
}

static void cmd_which(void)
{
    if (argc < 2) {
        printf("usage: which <name>\n");
        return;
    }

    for (int i = 1; i < argc; i++) {
        const struct ocean_boot_module_spec *module =
            ocean_find_boot_module_spec(argv[i]);
        const struct ocean_service_spec *service =
            ocean_find_service_spec(argv[i]);

        if (module) {
            printf("%s: %s [%s]\n",
                   argv[i], module->path, module->summary);
            continue;
        }

        if (service) {
            printf("%s: %s [service, priority %d]\n",
                   argv[i], service->path, service->priority);
            continue;
        }

        printf("%s: not found\n", argv[i]);
    }
}

static void cmd_version(void)
{
    printf("Ocean Shell v%s\n", SHELL_VERSION);
    printf("  loaded boot modules: %u\n", (unsigned)OCEAN_BOOT_MODULE_SPEC_COUNT);
    printf("  declared core services: %u\n", (unsigned)OCEAN_SERVICE_SPEC_COUNT);
}

static int resolve_external_path(const char *name, char *path, size_t path_size)
{
    const struct ocean_boot_module_spec *module;

    if (!name || !*name) {
        return -1;
    }

    if (name[0] == '/') {
        snprintf(path, path_size, "%s", name);
        return 0;
    }

    module = ocean_find_boot_module_spec(name);
    if (!module || !module->runnable_from_shell) {
        return -1;
    }

    snprintf(path, path_size, "%s", module->path);
    return 0;
}

static int wait_for_pid(int pid)
{
    while (1) {
        int status = 0;
        int child_pid = wait(&status);

        if (child_pid == pid) {
            return status;
        }

        if (child_pid < 0) {
            printf("sh: wait failed for pid %d\n", pid);
            return 1;
        }

        yield();
    }
}

/*
 * Execute external command
 */
static void exec_external(void)
{
    char path[64];

    if (resolve_external_path(argv[0], path, sizeof(path)) < 0) {
        printf("%s: command not found\n", argv[0]);
        printf("Use 'help' or 'modules' to inspect available commands.\n");
        return;
    }

    /* Fork and exec */
    int pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        return;
    }

    if (pid == 0) {
        exec(path, argv, NULL);
        /* If exec returns, it failed */
        printf("%s: exec failed\n", argv[0]);
        _exit(1);
    } else {
        /* Parent waits for the child it launched */
        int status = wait_for_pid(pid);
        if (status != 0) {
            printf("sh: %s exited with status %d\n", argv[0], status);
        }
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
    } else if (strcmp(argv[0], "cd") == 0) {
        cmd_cd();
    } else if (strcmp(argv[0], "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(argv[0], "echo") == 0) {
        cmd_echo();
    } else if (strcmp(argv[0], "pid") == 0) {
        cmd_pid();
    } else if (strcmp(argv[0], "clear") == 0) {
        cmd_clear();
    } else if (strcmp(argv[0], "modules") == 0) {
        cmd_modules();
    } else if (strcmp(argv[0], "services") == 0) {
        cmd_services();
    } else if (strcmp(argv[0], "which") == 0) {
        cmd_which();
    } else if (strcmp(argv[0], "version") == 0) {
        cmd_version();
    } else {
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
        print_prompt();

        int read_status = read_line();
        if (read_status == READ_LINE_EOF) {
            printf("\nEOF\n");
            break;
        }
        if (read_status == READ_LINE_TOO_LONG) {
            printf("\nsh: input exceeds %d characters\n", MAX_LINE - 1);
            continue;
        }

        if (parse_line() == 0) {
            execute();
        }
    }

    return 0;
}
