/*
 * Ocean VFS Server
 *
 * Virtual Filesystem server that handles:
 *   - File system namespace management
 *   - Path resolution and lookup
 *   - Mount point management
 *   - Routing requests to filesystem drivers
 *
 * The VFS is the central hub for all file operations,
 * delegating to specific filesystem drivers (ext2, ramfs, etc).
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define VFS_VERSION "0.1.0"
#define MAX_MOUNTS      16
#define MAX_OPEN_FILES  128
#define MAX_PATH        256

/* Mount point entry */
struct mount_entry {
    char     path[MAX_PATH];    /* Mount path */
    uint32_t fs_endpoint;       /* Filesystem driver endpoint */
    uint32_t root_inode;        /* Root inode of mounted fs */
    uint32_t flags;             /* Mount flags */
};

/* Open file entry */
struct open_file {
    uint32_t owner_pid;         /* Owning process */
    uint32_t mount_idx;         /* Mount point index */
    uint32_t inode;             /* File inode */
    uint64_t offset;            /* Current file offset */
    uint32_t flags;             /* Open flags */
    uint32_t refcount;          /* Reference count */
};

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Seek origins */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

static struct mount_entry mounts[MAX_MOUNTS];
static struct open_file files[MAX_OPEN_FILES];
static int num_mounts = 0;
static int num_open_files = 0;
static int vfs_endpoint = -1;

/* Statistics */
static uint64_t open_count = 0;
static uint64_t read_count = 0;
static uint64_t write_count = 0;
static uint64_t close_count = 0;

/*
 * Initialize the VFS server
 */
static void vfs_init(void)
{
    printf("[vfs] VFS Server v%s starting\n", VFS_VERSION);

    /* Initialize mount table */
    memset(mounts, 0, sizeof(mounts));
    memset(files, 0, sizeof(files));

    /* Create our IPC endpoint */
    vfs_endpoint = endpoint_create(0);
    if (vfs_endpoint < 0) {
        printf("[vfs] Failed to create endpoint: %d\n", vfs_endpoint);
        return;
    }
    printf("[vfs] Created endpoint %d\n", vfs_endpoint);

    printf("[vfs] VFS server initialized\n");
}

/*
 * Find mount point for a path
 */
static int find_mount(const char *path)
{
    int best_match = -1;
    size_t best_len = 0;

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].fs_endpoint == 0) continue;

        size_t mount_len = strlen(mounts[i].path);

        /* Check if path starts with mount path */
        if (strncmp(path, mounts[i].path, mount_len) == 0) {
            /* Must be exact match or followed by / */
            if (path[mount_len] == '\0' || path[mount_len] == '/') {
                if (mount_len > best_len) {
                    best_len = mount_len;
                    best_match = i;
                }
            }
        }
    }

    return best_match;
}

/*
 * Find free file slot
 */
static int alloc_file(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (files[i].refcount == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Handle VFS_MOUNT request
 */
static int handle_mount(const char *source, const char *target,
                        uint32_t fs_endpoint, uint32_t flags)
{
    (void)source;

    if (num_mounts >= MAX_MOUNTS) {
        printf("[vfs] Mount table full\n");
        return E_NOMEM;
    }

    /* Find free mount slot */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].fs_endpoint == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return E_NOMEM;
    }

    /* Check for existing mount at this path */
    if (find_mount(target) >= 0) {
        printf("[vfs] Already mounted at %s\n", target);
        return E_EXIST;
    }

    /* Add mount entry */
    strncpy(mounts[slot].path, target, MAX_PATH - 1);
    mounts[slot].fs_endpoint = fs_endpoint;
    mounts[slot].root_inode = 1;  /* Root inode is typically 1 */
    mounts[slot].flags = flags;
    num_mounts++;

    printf("[vfs] Mounted filesystem at %s (endpoint %u)\n",
           target, fs_endpoint);

    return E_OK;
}

/*
 * Handle VFS_OPEN request
 */
static int handle_open(uint32_t pid, const char *path, uint32_t flags,
                       uint32_t mode, int *out_fd)
{
    (void)mode;
    open_count++;

    /* Find mount point */
    int mount_idx = find_mount(path);
    if (mount_idx < 0) {
        printf("[vfs] No mount point for %s\n", path);
        return E_NOENT;
    }

    /* Allocate file descriptor */
    int fd = alloc_file();
    if (fd < 0) {
        printf("[vfs] No free file descriptors\n");
        return E_NOMEM;
    }

    /* TODO: Send FS_LOOKUP to filesystem driver to get inode */
    uint32_t inode = 42;  /* Fake inode for now */

    /* Initialize file entry */
    files[fd].owner_pid = pid;
    files[fd].mount_idx = (uint32_t)mount_idx;
    files[fd].inode = inode;
    files[fd].offset = 0;
    files[fd].flags = flags;
    files[fd].refcount = 1;
    num_open_files++;

    *out_fd = fd;

    printf("[vfs] Opened %s as fd %d for PID %u\n", path, fd, pid);

    return E_OK;
}

/*
 * Handle VFS_CLOSE request
 */
static int handle_close(uint32_t pid, int fd)
{
    close_count++;

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return E_INVAL;
    }

    if (files[fd].refcount == 0) {
        return E_INVAL;
    }

    if (files[fd].owner_pid != pid) {
        return E_PERM;
    }

    files[fd].refcount--;
    if (files[fd].refcount == 0) {
        memset(&files[fd], 0, sizeof(files[fd]));
        num_open_files--;
    }

    printf("[vfs] Closed fd %d for PID %u\n", fd, pid);

    return E_OK;
}

/*
 * Handle VFS_READ request
 */
static int handle_read(uint32_t pid, int fd, void *buf, size_t count,
                       size_t *bytes_read)
{
    (void)buf;
    read_count++;

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return E_INVAL;
    }

    if (files[fd].refcount == 0 || files[fd].owner_pid != pid) {
        return E_INVAL;
    }

    /* TODO: Send FS_READ to filesystem driver */

    /* Fake read for now */
    *bytes_read = count > 0 ? 1 : 0;
    files[fd].offset += *bytes_read;

    return E_OK;
}

/*
 * Handle VFS_WRITE request
 */
static int handle_write(uint32_t pid, int fd, const void *buf, size_t count,
                        size_t *bytes_written)
{
    (void)buf;
    write_count++;

    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return E_INVAL;
    }

    if (files[fd].refcount == 0 || files[fd].owner_pid != pid) {
        return E_INVAL;
    }

    /* Check write permission */
    if ((files[fd].flags & O_WRONLY) == 0 &&
        (files[fd].flags & O_RDWR) == 0) {
        return E_PERM;
    }

    /* TODO: Send FS_WRITE to filesystem driver */

    *bytes_written = count;
    files[fd].offset += count;

    return E_OK;
}

/*
 * Handle VFS_LSEEK request
 */
static int handle_lseek(uint32_t pid, int fd, int64_t offset, int whence,
                        uint64_t *new_offset)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return E_INVAL;
    }

    if (files[fd].refcount == 0 || files[fd].owner_pid != pid) {
        return E_INVAL;
    }

    /* TODO: Get file size from filesystem driver */
    uint64_t file_size = 1024;  /* Fake size */

    uint64_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = (uint64_t)offset;
            break;
        case SEEK_CUR:
            new_pos = files[fd].offset + offset;
            break;
        case SEEK_END:
            new_pos = file_size + offset;
            break;
        default:
            return E_INVAL;
    }

    files[fd].offset = new_pos;
    *new_offset = new_pos;

    return E_OK;
}

/*
 * Process incoming IPC messages
 */
static void vfs_serve(void)
{
    printf("[vfs] Entering service loop\n");

    /* For now, do some self-testing */
    for (int i = 0; i < 50; i++) {
        yield();

        /* Mount root filesystem */
        if (i == 5) {
            int err = handle_mount("ram", "/", 100, 0);
            if (err == E_OK) {
                printf("[vfs] Self-test: mounted root\n");
            }
        }

        /* Open a file */
        if (i == 10) {
            int fd = -1;
            int err = handle_open(1, "/test.txt", O_RDONLY, 0644, &fd);
            if (err == E_OK) {
                printf("[vfs] Self-test: opened fd %d\n", fd);
            }
        }

        /* Read from file */
        if (i == 15) {
            size_t bytes = 0;
            char buf[64];
            int err = handle_read(1, 0, buf, sizeof(buf), &bytes);
            if (err == E_OK) {
                printf("[vfs] Self-test: read %zu bytes\n", bytes);
            }
        }

        /* Seek */
        if (i == 20) {
            uint64_t pos = 0;
            int err = handle_lseek(1, 0, 0, SEEK_SET, &pos);
            if (err == E_OK) {
                printf("[vfs] Self-test: seeked to %llu\n",
                       (unsigned long long)pos);
            }
        }

        /* Close file */
        if (i == 25) {
            int err = handle_close(1, 0);
            if (err == E_OK) {
                printf("[vfs] Self-test: closed fd 0\n");
            }
        }
    }
}

/*
 * Print VFS state
 */
static void vfs_dump(void)
{
    printf("\n[vfs] Mount Table:\n");
    printf("  PATH         ENDPOINT  FLAGS\n");
    printf("  -----------  --------  -----\n");

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].fs_endpoint != 0) {
            printf("  %-11s  %-8u  0x%x\n",
                   mounts[i].path, mounts[i].fs_endpoint, mounts[i].flags);
        }
    }

    printf("\n[vfs] Open Files: %d\n", num_open_files);

    printf("\n[vfs] Statistics:\n");
    printf("  Open calls: %llu\n", (unsigned long long)open_count);
    printf("  Read calls: %llu\n", (unsigned long long)read_count);
    printf("  Write calls: %llu\n", (unsigned long long)write_count);
    printf("  Close calls: %llu\n", (unsigned long long)close_count);
    printf("\n");
}

/*
 * Main entry point
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n========================================\n");
    printf("  Ocean VFS Server v%s\n", VFS_VERSION);
    printf("========================================\n\n");

    printf("[vfs] PID: %d, PPID: %d\n", getpid(), getppid());

    vfs_init();
    vfs_serve();
    vfs_dump();

    printf("[vfs] VFS server exiting\n");
    return 0;
}
