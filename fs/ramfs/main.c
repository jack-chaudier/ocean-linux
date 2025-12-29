/*
 * Ocean RAMFS Driver
 *
 * RAM-based filesystem driver that stores all files in memory.
 * Used for the initial root filesystem before real disk access.
 *
 * Features:
 *   - In-memory inode and data storage
 *   - Directory support
 *   - Basic file operations (read, write, create, unlink)
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define RAMFS_VERSION   "0.1.0"
#define MAX_INODES      128
#define MAX_NAME        64
#define MAX_DATA_SIZE   4096
#define MAX_DIR_ENTRIES 32

/* Inode types */
#define INODE_FREE      0
#define INODE_FILE      1
#define INODE_DIR       2

/* Directory entry */
struct ramfs_dirent {
    uint32_t inode;             /* Inode number */
    char     name[MAX_NAME];    /* Entry name */
};

/* Inode structure */
struct ramfs_inode {
    uint32_t type;              /* Inode type */
    uint32_t mode;              /* Permissions */
    uint32_t uid;               /* Owner UID */
    uint32_t gid;               /* Owner GID */
    uint32_t nlink;             /* Link count */
    uint64_t size;              /* File size */
    uint64_t atime;             /* Access time */
    uint64_t mtime;             /* Modification time */
    uint64_t ctime;             /* Status change time */

    union {
        /* File data */
        struct {
            uint8_t data[MAX_DATA_SIZE];
        } file;

        /* Directory entries */
        struct {
            struct ramfs_dirent entries[MAX_DIR_ENTRIES];
            uint32_t count;
        } dir;
    };
};

static struct ramfs_inode inodes[MAX_INODES];
static int num_inodes = 0;
static int ramfs_endpoint = -1;

/* Statistics */
static uint64_t read_ops = 0;
static uint64_t write_ops = 0;
static uint64_t lookup_ops = 0;
static uint64_t create_ops = 0;

/*
 * Allocate a new inode
 */
static int alloc_inode(void)
{
    for (int i = 1; i < MAX_INODES; i++) {  /* Skip inode 0 */
        if (inodes[i].type == INODE_FREE) {
            return i;
        }
    }
    return -1;
}

/*
 * Initialize an inode
 */
static void init_inode(int ino, int type, uint32_t mode)
{
    memset(&inodes[ino], 0, sizeof(struct ramfs_inode));
    inodes[ino].type = type;
    inodes[ino].mode = mode;
    inodes[ino].nlink = 1;
    inodes[ino].atime = 0;  /* TODO: Get real time */
    inodes[ino].mtime = 0;
    inodes[ino].ctime = 0;
    num_inodes++;
}

/*
 * Find directory entry by name
 */
static int dir_lookup(int dir_ino, const char *name)
{
    lookup_ops++;

    if (inodes[dir_ino].type != INODE_DIR) {
        return -1;
    }

    struct ramfs_inode *dir = &inodes[dir_ino];
    for (uint32_t i = 0; i < dir->dir.count; i++) {
        if (strcmp(dir->dir.entries[i].name, name) == 0) {
            return (int)dir->dir.entries[i].inode;
        }
    }

    return -1;
}

/*
 * Add entry to directory
 */
static int dir_add_entry(int dir_ino, const char *name, int entry_ino)
{
    if (inodes[dir_ino].type != INODE_DIR) {
        return E_INVAL;
    }

    struct ramfs_inode *dir = &inodes[dir_ino];
    if (dir->dir.count >= MAX_DIR_ENTRIES) {
        return E_NOMEM;
    }

    /* Check for duplicate */
    if (dir_lookup(dir_ino, name) >= 0) {
        return E_EXIST;
    }

    int idx = (int)dir->dir.count;
    dir->dir.entries[idx].inode = (uint32_t)entry_ino;
    strncpy(dir->dir.entries[idx].name, name, MAX_NAME - 1);
    dir->dir.count++;

    return E_OK;
}

/*
 * Remove entry from directory
 */
static int dir_remove_entry(int dir_ino, const char *name)
{
    if (inodes[dir_ino].type != INODE_DIR) {
        return E_INVAL;
    }

    struct ramfs_inode *dir = &inodes[dir_ino];

    for (uint32_t i = 0; i < dir->dir.count; i++) {
        if (strcmp(dir->dir.entries[i].name, name) == 0) {
            /* Shift remaining entries */
            for (uint32_t j = i; j < dir->dir.count - 1; j++) {
                dir->dir.entries[j] = dir->dir.entries[j + 1];
            }
            dir->dir.count--;
            return E_OK;
        }
    }

    return E_NOENT;
}

/*
 * Initialize ramfs with root directory
 */
static void ramfs_init(void)
{
    printf("[ramfs] RAMFS Driver v%s starting\n", RAMFS_VERSION);

    /* Initialize inodes */
    memset(inodes, 0, sizeof(inodes));

    /* Create root directory (inode 1) */
    init_inode(1, INODE_DIR, 0755);
    inodes[1].nlink = 2;  /* . and parent */

    /* Add . and .. entries */
    dir_add_entry(1, ".", 1);
    dir_add_entry(1, "..", 1);

    /* Create our IPC endpoint */
    ramfs_endpoint = endpoint_create(0);
    if (ramfs_endpoint < 0) {
        printf("[ramfs] Failed to create endpoint: %d\n", ramfs_endpoint);
        return;
    }
    printf("[ramfs] Created endpoint %d\n", ramfs_endpoint);

    printf("[ramfs] RAMFS initialized with root directory\n");
}

/*
 * Handle FS_LOOKUP request
 */
static int handle_lookup(int dir_ino, const char *name, int *out_ino)
{
    int ino = dir_lookup(dir_ino, name);
    if (ino < 0) {
        return E_NOENT;
    }

    *out_ino = ino;
    return E_OK;
}

/*
 * Handle FS_CREATE request
 */
static int handle_create(int dir_ino, const char *name, uint32_t mode,
                         int *out_ino)
{
    create_ops++;

    int ino = alloc_inode();
    if (ino < 0) {
        return E_NOMEM;
    }

    init_inode(ino, INODE_FILE, mode);

    int err = dir_add_entry(dir_ino, name, ino);
    if (err != E_OK) {
        inodes[ino].type = INODE_FREE;
        num_inodes--;
        return err;
    }

    *out_ino = ino;
    printf("[ramfs] Created file '%s' as inode %d\n", name, ino);

    return E_OK;
}

/*
 * Handle FS_MKDIR request
 */
static int handle_mkdir(int parent_ino, const char *name, uint32_t mode,
                        int *out_ino)
{
    int ino = alloc_inode();
    if (ino < 0) {
        return E_NOMEM;
    }

    init_inode(ino, INODE_DIR, mode);
    inodes[ino].nlink = 2;

    /* Add . and .. entries */
    dir_add_entry(ino, ".", ino);
    dir_add_entry(ino, "..", parent_ino);

    int err = dir_add_entry(parent_ino, name, ino);
    if (err != E_OK) {
        inodes[ino].type = INODE_FREE;
        num_inodes--;
        return err;
    }

    inodes[parent_ino].nlink++;
    *out_ino = ino;

    printf("[ramfs] Created directory '%s' as inode %d\n", name, ino);

    return E_OK;
}

/*
 * Handle FS_READ request
 */
static int handle_read(int ino, uint64_t offset, void *buf, size_t count,
                       size_t *bytes_read)
{
    read_ops++;

    if (inodes[ino].type != INODE_FILE) {
        return E_INVAL;
    }

    struct ramfs_inode *file = &inodes[ino];

    if (offset >= file->size) {
        *bytes_read = 0;
        return E_OK;
    }

    size_t avail = (size_t)(file->size - offset);
    size_t to_read = count < avail ? count : avail;

    memcpy(buf, file->file.data + offset, to_read);
    *bytes_read = to_read;

    return E_OK;
}

/*
 * Handle FS_WRITE request
 */
static int handle_write(int ino, uint64_t offset, const void *buf,
                        size_t count, size_t *bytes_written)
{
    write_ops++;

    if (inodes[ino].type != INODE_FILE) {
        return E_INVAL;
    }

    struct ramfs_inode *file = &inodes[ino];

    if (offset + count > MAX_DATA_SIZE) {
        count = MAX_DATA_SIZE - offset;
    }

    memcpy(file->file.data + offset, buf, count);

    if (offset + count > file->size) {
        file->size = offset + count;
    }

    *bytes_written = count;

    return E_OK;
}

/*
 * Handle FS_STAT request
 */
static int handle_stat(int ino, struct vfs_stat *st)
{
    if (inodes[ino].type == INODE_FREE) {
        return E_NOENT;
    }

    struct ramfs_inode *inode = &inodes[ino];

    st->mode = inode->mode;
    if (inode->type == INODE_DIR) {
        st->mode |= S_IFDIR;
    } else {
        st->mode |= S_IFREG;
    }
    st->size = inode->size;
    st->nlink = inode->nlink;
    st->uid = inode->uid;
    st->gid = inode->gid;
    st->atime = inode->atime;
    st->mtime = inode->mtime;
    st->ctime = inode->ctime;

    return E_OK;
}

/*
 * Handle FS_UNLINK request
 */
static int handle_unlink(int dir_ino, const char *name)
{
    int ino = dir_lookup(dir_ino, name);
    if (ino < 0) {
        return E_NOENT;
    }

    if (inodes[ino].type == INODE_DIR) {
        return E_PERM;  /* Use rmdir for directories */
    }

    inodes[ino].nlink--;
    if (inodes[ino].nlink == 0) {
        inodes[ino].type = INODE_FREE;
        num_inodes--;
    }

    return dir_remove_entry(dir_ino, name);
}

/*
 * Process incoming IPC messages
 */
static void ramfs_serve(void)
{
    printf("[ramfs] Entering service loop\n");

    /* Self-test */
    for (int i = 0; i < 50; i++) {
        yield();

        /* Create a test file */
        if (i == 5) {
            int ino = 0;
            int err = handle_create(1, "test.txt", 0644, &ino);
            if (err == E_OK) {
                printf("[ramfs] Self-test: created inode %d\n", ino);
            }
        }

        /* Write to file */
        if (i == 10) {
            const char *data = "Hello, Ocean!";
            size_t written = 0;
            int err = handle_write(2, 0, data, strlen(data), &written);
            if (err == E_OK) {
                printf("[ramfs] Self-test: wrote %zu bytes\n", written);
            }
        }

        /* Read from file */
        if (i == 15) {
            char buf[64];
            size_t bytes = 0;
            int err = handle_read(2, 0, buf, sizeof(buf) - 1, &bytes);
            if (err == E_OK) {
                buf[bytes] = '\0';
                printf("[ramfs] Self-test: read '%s'\n", buf);
            }
        }

        /* Create a directory */
        if (i == 20) {
            int ino = 0;
            int err = handle_mkdir(1, "bin", 0755, &ino);
            if (err == E_OK) {
                printf("[ramfs] Self-test: created dir inode %d\n", ino);
            }
        }

        /* Create file in subdirectory */
        if (i == 25) {
            int ino = 0;
            int err = handle_create(3, "sh", 0755, &ino);
            if (err == E_OK) {
                printf("[ramfs] Self-test: created /bin/sh inode %d\n", ino);
            }
        }

        /* Stat a file */
        if (i == 30) {
            struct vfs_stat st;
            int err = handle_stat(2, &st);
            if (err == E_OK) {
                printf("[ramfs] Self-test: stat size=%llu mode=0%llo\n",
                       (unsigned long long)st.size,
                       (unsigned long long)st.mode);
            }
        }
    }
}

/*
 * Dump filesystem structure
 */
static void ramfs_dump(void)
{
    printf("\n[ramfs] Filesystem Structure:\n");
    printf("  INO  TYPE  MODE    SIZE  NLINK  NAME\n");
    printf("  ---  ----  ------  ----  -----  ----\n");

    for (int i = 1; i < MAX_INODES; i++) {
        if (inodes[i].type != INODE_FREE) {
            const char *type_str = inodes[i].type == INODE_DIR ? "DIR " : "FILE";
            printf("  %-3d  %s  0%04o   %-4llu  %-5u\n",
                   i, type_str,
                   inodes[i].mode,
                   (unsigned long long)inodes[i].size,
                   inodes[i].nlink);
        }
    }

    printf("\n[ramfs] Root directory contents:\n");
    struct ramfs_inode *root = &inodes[1];
    for (uint32_t i = 0; i < root->dir.count; i++) {
        printf("  %s -> inode %u\n",
               root->dir.entries[i].name,
               root->dir.entries[i].inode);
    }

    printf("\n[ramfs] Statistics:\n");
    printf("  Active inodes: %d\n", num_inodes);
    printf("  Lookup ops: %llu\n", (unsigned long long)lookup_ops);
    printf("  Create ops: %llu\n", (unsigned long long)create_ops);
    printf("  Read ops: %llu\n", (unsigned long long)read_ops);
    printf("  Write ops: %llu\n", (unsigned long long)write_ops);
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
    printf("  Ocean RAMFS Driver v%s\n", RAMFS_VERSION);
    printf("========================================\n\n");

    printf("[ramfs] PID: %d, PPID: %d\n", getpid(), getppid());

    ramfs_init();
    ramfs_serve();
    ramfs_dump();

    printf("[ramfs] RAMFS driver exiting\n");
    return 0;
}
