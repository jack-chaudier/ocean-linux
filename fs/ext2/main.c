/*
 * Ocean Ext2 Filesystem Driver
 *
 * Read-only ext2 filesystem support:
 *   - Superblock parsing
 *   - Block group descriptors
 *   - Inode reading
 *   - Directory traversal
 *   - File data reading
 *
 * Based on the ext2 specification.
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define EXT2_VERSION "0.1.0"

/* Ext2 magic number */
#define EXT2_MAGIC          0xEF53

/* Ext2 superblock location */
#define EXT2_SUPERBLOCK_OFFSET  1024
#define EXT2_SUPERBLOCK_SIZE    1024

/* Inode numbers */
#define EXT2_ROOT_INODE     2
#define EXT2_BAD_INO        1

/* File types (from inode mode) */
#define EXT2_S_IFSOCK   0xC000
#define EXT2_S_IFLNK    0xA000
#define EXT2_S_IFREG    0x8000
#define EXT2_S_IFBLK    0x6000
#define EXT2_S_IFDIR    0x4000
#define EXT2_S_IFCHR    0x2000
#define EXT2_S_IFIFO    0x1000
#define EXT2_S_IFMT     0xF000

/* Directory entry file types */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

/* Superblock structure */
struct ext2_superblock {
    uint32_t s_inodes_count;        /* Total inodes */
    uint32_t s_blocks_count;        /* Total blocks */
    uint32_t s_r_blocks_count;      /* Reserved blocks */
    uint32_t s_free_blocks_count;   /* Free blocks */
    uint32_t s_free_inodes_count;   /* Free inodes */
    uint32_t s_first_data_block;    /* First data block */
    uint32_t s_log_block_size;      /* Block size = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;       /* Fragment size */
    uint32_t s_blocks_per_group;    /* Blocks per group */
    uint32_t s_frags_per_group;     /* Fragments per group */
    uint32_t s_inodes_per_group;    /* Inodes per group */
    uint32_t s_mtime;               /* Mount time */
    uint32_t s_wtime;               /* Write time */
    uint16_t s_mnt_count;           /* Mount count */
    uint16_t s_max_mnt_count;       /* Max mount count */
    uint16_t s_magic;               /* Magic number (0xEF53) */
    uint16_t s_state;               /* FS state */
    uint16_t s_errors;              /* Error handling */
    uint16_t s_minor_rev_level;     /* Minor revision */
    uint32_t s_lastcheck;           /* Last check time */
    uint32_t s_checkinterval;       /* Check interval */
    uint32_t s_creator_os;          /* Creator OS */
    uint32_t s_rev_level;           /* Revision level */
    uint16_t s_def_resuid;          /* Default reserved UID */
    uint16_t s_def_resgid;          /* Default reserved GID */
    /* Extended fields (rev 1+) */
    uint32_t s_first_ino;           /* First non-reserved inode */
    uint16_t s_inode_size;          /* Inode size */
    uint16_t s_block_group_nr;      /* Block group of this superblock */
    uint32_t s_feature_compat;      /* Compatible features */
    uint32_t s_feature_incompat;    /* Incompatible features */
    uint32_t s_feature_ro_compat;   /* Read-only compatible features */
    uint8_t  s_uuid[16];            /* Filesystem UUID */
    char     s_volume_name[16];     /* Volume name */
    char     s_last_mounted[64];    /* Last mount path */
    uint32_t s_algo_bitmap;         /* Compression algorithm */
    /* Performance hints */
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    /* Journaling (ext3) */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
} __attribute__((packed));

/* Block group descriptor */
struct ext2_group_desc {
    uint32_t bg_block_bitmap;       /* Block bitmap block */
    uint32_t bg_inode_bitmap;       /* Inode bitmap block */
    uint32_t bg_inode_table;        /* Inode table block */
    uint16_t bg_free_blocks_count;  /* Free blocks in group */
    uint16_t bg_free_inodes_count;  /* Free inodes in group */
    uint16_t bg_used_dirs_count;    /* Directories in group */
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

/* Inode structure */
struct ext2_inode {
    uint16_t i_mode;                /* File mode */
    uint16_t i_uid;                 /* Owner UID */
    uint32_t i_size;                /* Size in bytes */
    uint32_t i_atime;               /* Access time */
    uint32_t i_ctime;               /* Creation time */
    uint32_t i_mtime;               /* Modification time */
    uint32_t i_dtime;               /* Deletion time */
    uint16_t i_gid;                 /* Owner GID */
    uint16_t i_links_count;         /* Links count */
    uint32_t i_blocks;              /* Blocks count (512-byte units) */
    uint32_t i_flags;               /* File flags */
    uint32_t i_osd1;                /* OS-dependent value 1 */
    uint32_t i_block[15];           /* Block pointers */
    uint32_t i_generation;          /* File version (NFS) */
    uint32_t i_file_acl;            /* File ACL */
    uint32_t i_dir_acl;             /* Directory ACL / size_high */
    uint32_t i_faddr;               /* Fragment address */
    uint8_t  i_osd2[12];            /* OS-dependent value 2 */
} __attribute__((packed));

/* Directory entry */
struct ext2_dir_entry {
    uint32_t inode;                 /* Inode number */
    uint16_t rec_len;               /* Record length */
    uint8_t  name_len;              /* Name length */
    uint8_t  file_type;             /* File type */
    char     name[];                /* File name */
} __attribute__((packed));

/* Filesystem state */
struct ext2_fs {
    uint32_t block_size;            /* Block size in bytes */
    uint32_t inodes_per_group;      /* Inodes per group */
    uint32_t blocks_per_group;      /* Blocks per group */
    uint32_t inode_size;            /* Inode size */
    uint32_t group_count;           /* Number of block groups */
    uint32_t first_data_block;      /* First data block */
    uint32_t dev_id;                /* Block device ID */

    struct ext2_superblock sb;      /* Superblock copy */
    struct ext2_group_desc *groups; /* Group descriptors */

    uint8_t block_buffer[4096];     /* Block read buffer */
};

static struct ext2_fs ext2;
static int ext2_endpoint = -1;
static int mounted = 0;

/* Statistics */
static uint64_t blocks_read = 0;
static uint64_t inodes_read = 0;
static uint64_t dir_lookups = 0;

/*
 * Simulated block read (until BLK server IPC works)
 */
static int read_block(uint32_t block_num, void *buffer)
{
    /* TODO: Send BLK_READ to block server
     * For now, just zero the buffer
     */
    (void)block_num;
    memset(buffer, 0, ext2.block_size);
    blocks_read++;
    return E_OK;
}

/*
 * Read multiple blocks
 */
static int read_blocks(uint32_t start_block, uint32_t count, void *buffer)
{
    uint8_t *buf = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        int err = read_block(start_block + i, buf);
        if (err != E_OK) return err;
        buf += ext2.block_size;
    }
    return E_OK;
}

/*
 * Read inode by number
 */
static int read_inode(uint32_t inode_num, struct ext2_inode *inode)
{
    if (inode_num == 0 || inode_num > ext2.sb.s_inodes_count) {
        return E_INVAL;
    }

    /* Calculate which block group contains this inode */
    uint32_t group = (inode_num - 1) / ext2.inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2.inodes_per_group;

    /* Get inode table block for this group */
    uint32_t inode_table = ext2.groups[group].bg_inode_table;

    /* Calculate which block and offset within block */
    uint32_t inodes_per_block = ext2.block_size / ext2.inode_size;
    uint32_t block = inode_table + (index / inodes_per_block);
    uint32_t offset = (index % inodes_per_block) * ext2.inode_size;

    /* Read the block */
    int err = read_block(block, ext2.block_buffer);
    if (err != E_OK) return err;

    /* Copy inode data */
    memcpy(inode, ext2.block_buffer + offset, sizeof(struct ext2_inode));
    inodes_read++;

    return E_OK;
}

/*
 * Get data block number from inode (handles indirect blocks)
 */
static uint32_t get_data_block(struct ext2_inode *inode, uint32_t block_index)
{
    uint32_t ptrs_per_block = ext2.block_size / 4;

    /* Direct blocks (0-11) */
    if (block_index < 12) {
        return inode->i_block[block_index];
    }
    block_index -= 12;

    /* Single indirect (12) */
    if (block_index < ptrs_per_block) {
        if (inode->i_block[12] == 0) return 0;

        read_block(inode->i_block[12], ext2.block_buffer);
        return ((uint32_t *)ext2.block_buffer)[block_index];
    }
    block_index -= ptrs_per_block;

    /* Double indirect (13) */
    if (block_index < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) return 0;

        read_block(inode->i_block[13], ext2.block_buffer);
        uint32_t indirect = ((uint32_t *)ext2.block_buffer)[block_index / ptrs_per_block];
        if (indirect == 0) return 0;

        read_block(indirect, ext2.block_buffer);
        return ((uint32_t *)ext2.block_buffer)[block_index % ptrs_per_block];
    }
    block_index -= ptrs_per_block * ptrs_per_block;

    /* Triple indirect (14) - rarely used */
    if (inode->i_block[14] == 0) return 0;

    /* ... implementation similar to double indirect */

    return 0;
}

/*
 * Read file data
 */
static int read_file_data(struct ext2_inode *inode, uint64_t offset,
                          void *buffer, size_t size, size_t *bytes_read)
{
    if (offset >= inode->i_size) {
        *bytes_read = 0;
        return E_OK;
    }

    /* Limit read to file size */
    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }

    uint8_t *buf = (uint8_t *)buffer;
    size_t total = 0;

    while (size > 0) {
        uint32_t block_index = offset / ext2.block_size;
        uint32_t block_offset = offset % ext2.block_size;
        uint32_t block_num = get_data_block(inode, block_index);

        if (block_num == 0) {
            /* Sparse file - zero fill */
            memset(ext2.block_buffer, 0, ext2.block_size);
        } else {
            int err = read_block(block_num, ext2.block_buffer);
            if (err != E_OK) return err;
        }

        size_t chunk = ext2.block_size - block_offset;
        if (chunk > size) chunk = size;

        memcpy(buf, ext2.block_buffer + block_offset, chunk);

        buf += chunk;
        offset += chunk;
        size -= chunk;
        total += chunk;
    }

    *bytes_read = total;
    return E_OK;
}

/*
 * Look up name in directory
 */
static int dir_lookup(struct ext2_inode *dir, const char *name, uint32_t *out_inode)
{
    dir_lookups++;

    if ((dir->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return E_INVAL;
    }

    size_t name_len = strlen(name);
    uint32_t offset = 0;

    while (offset < dir->i_size) {
        uint32_t block_index = offset / ext2.block_size;
        uint32_t block_offset = offset % ext2.block_size;
        uint32_t block_num = get_data_block(dir, block_index);

        if (block_num == 0) {
            offset += ext2.block_size;
            continue;
        }

        int err = read_block(block_num, ext2.block_buffer);
        if (err != E_OK) return err;

        while (block_offset < ext2.block_size) {
            struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(ext2.block_buffer + block_offset);

            if (entry->rec_len == 0) break;

            if (entry->inode != 0 &&
                entry->name_len == name_len &&
                memcmp(entry->name, name, name_len) == 0) {
                *out_inode = entry->inode;
                return E_OK;
            }

            block_offset += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    return E_NOENT;
}

/*
 * Resolve path to inode
 */
static int resolve_path(const char *path, uint32_t *out_inode)
{
    if (!path || path[0] != '/') {
        return E_INVAL;
    }

    /* Start at root inode */
    uint32_t current_inode = EXT2_ROOT_INODE;
    struct ext2_inode inode;

    path++;  /* Skip leading slash */

    while (*path) {
        /* Skip multiple slashes */
        while (*path == '/') path++;
        if (*path == '\0') break;

        /* Extract component */
        char component[256];
        int i = 0;
        while (*path && *path != '/' && i < 255) {
            component[i++] = *path++;
        }
        component[i] = '\0';

        /* Read current directory inode */
        int err = read_inode(current_inode, &inode);
        if (err != E_OK) return err;

        /* Look up component */
        err = dir_lookup(&inode, component, &current_inode);
        if (err != E_OK) return err;
    }

    *out_inode = current_inode;
    return E_OK;
}

/*
 * List directory contents
 */
static int list_directory(struct ext2_inode *dir)
{
    if ((dir->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return E_INVAL;
    }

    printf("  INO       SIZE  TYPE  NAME\n");
    printf("  -----  -------  ----  ----\n");

    uint32_t offset = 0;
    while (offset < dir->i_size) {
        uint32_t block_index = offset / ext2.block_size;
        uint32_t block_offset = offset % ext2.block_size;
        uint32_t block_num = get_data_block(dir, block_index);

        if (block_num == 0) {
            offset += ext2.block_size;
            continue;
        }

        read_block(block_num, ext2.block_buffer);

        while (block_offset < ext2.block_size) {
            struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(ext2.block_buffer + block_offset);

            if (entry->rec_len == 0) break;

            if (entry->inode != 0) {
                char name[256];
                memcpy(name, entry->name, entry->name_len);
                name[entry->name_len] = '\0';

                const char *type;
                switch (entry->file_type) {
                    case EXT2_FT_REG_FILE: type = "FILE"; break;
                    case EXT2_FT_DIR:      type = "DIR "; break;
                    case EXT2_FT_SYMLINK:  type = "LINK"; break;
                    case EXT2_FT_CHRDEV:   type = "CHR "; break;
                    case EXT2_FT_BLKDEV:   type = "BLK "; break;
                    default:               type = "??? "; break;
                }

                /* Get file size */
                struct ext2_inode file_inode;
                read_inode(entry->inode, &file_inode);

                printf("  %-5u  %7u  %s  %s\n",
                       entry->inode,
                       file_inode.i_size,
                       type,
                       name);
            }

            block_offset += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    return E_OK;
}

/*
 * Mount ext2 filesystem
 */
static int ext2_mount(uint32_t dev_id)
{
    printf("[ext2] Mounting ext2 filesystem from device %u\n", dev_id);

    ext2.dev_id = dev_id;

    /* Read superblock (at offset 1024) */
    /* For simulation, create a fake superblock */
    memset(&ext2.sb, 0, sizeof(ext2.sb));
    ext2.sb.s_magic = EXT2_MAGIC;
    ext2.sb.s_inodes_count = 1024;
    ext2.sb.s_blocks_count = 8192;
    ext2.sb.s_log_block_size = 0;  /* 1024 bytes */
    ext2.sb.s_blocks_per_group = 8192;
    ext2.sb.s_inodes_per_group = 1024;
    ext2.sb.s_first_data_block = 1;
    ext2.sb.s_rev_level = 1;
    ext2.sb.s_inode_size = 128;
    strncpy(ext2.sb.s_volume_name, "ocean-root", sizeof(ext2.sb.s_volume_name) - 1);

    /* Check magic */
    if (ext2.sb.s_magic != EXT2_MAGIC) {
        printf("[ext2] Invalid superblock magic: 0x%04x\n", ext2.sb.s_magic);
        return E_INVAL;
    }

    /* Calculate filesystem parameters */
    ext2.block_size = 1024 << ext2.sb.s_log_block_size;
    ext2.inodes_per_group = ext2.sb.s_inodes_per_group;
    ext2.blocks_per_group = ext2.sb.s_blocks_per_group;
    ext2.first_data_block = ext2.sb.s_first_data_block;

    if (ext2.sb.s_rev_level >= 1) {
        ext2.inode_size = ext2.sb.s_inode_size;
    } else {
        ext2.inode_size = 128;
    }

    ext2.group_count = (ext2.sb.s_blocks_count + ext2.blocks_per_group - 1) / ext2.blocks_per_group;

    printf("[ext2] Filesystem info:\n");
    printf("[ext2]   Volume: %s\n", ext2.sb.s_volume_name);
    printf("[ext2]   Block size: %u bytes\n", ext2.block_size);
    printf("[ext2]   Total blocks: %u\n", ext2.sb.s_blocks_count);
    printf("[ext2]   Total inodes: %u\n", ext2.sb.s_inodes_count);
    printf("[ext2]   Block groups: %u\n", ext2.group_count);

    /* Allocate group descriptors (simulated) */
    ext2.groups = (struct ext2_group_desc *)ext2.block_buffer;
    memset(ext2.groups, 0, sizeof(struct ext2_group_desc));
    ext2.groups[0].bg_inode_table = 3;  /* Fake inode table location */

    mounted = 1;
    printf("[ext2] Filesystem mounted successfully\n");

    return E_OK;
}

/*
 * Initialize ext2 driver
 */
static void ext2_init(void)
{
    printf("[ext2] Ext2 Driver v%s starting\n", EXT2_VERSION);

    memset(&ext2, 0, sizeof(ext2));

    ext2_endpoint = endpoint_create(0);
    if (ext2_endpoint < 0) {
        printf("[ext2] Failed to create endpoint\n");
        return;
    }
    printf("[ext2] Created endpoint %d\n", ext2_endpoint);

    /* Try to mount a simulated filesystem */
    ext2_mount(1);

    printf("[ext2] Ext2 driver initialized\n");
}

/*
 * Service loop
 */
static void ext2_serve(void)
{
    printf("[ext2] Entering service loop\n");

    for (int i = 0; i < 50; i++) {
        yield();

        /* Self-test: resolve root path */
        if (i == 10 && mounted) {
            uint32_t ino;
            int err = resolve_path("/", &ino);
            if (err == E_OK) {
                printf("[ext2] Self-test: resolved '/' to inode %u\n", ino);
            }
        }

        /* Self-test: list root directory (simulated) */
        if (i == 20 && mounted) {
            printf("[ext2] Self-test: root directory listing (simulated):\n");
            /* Since we don't have real data, just show what we would do */
            printf("  (would list root directory contents here)\n");
        }
    }
}

/*
 * Print filesystem info
 */
static void ext2_dump(void)
{
    printf("\n[ext2] Filesystem Status:\n");

    if (mounted) {
        printf("  Volume: %s\n", ext2.sb.s_volume_name);
        printf("  Block size: %u bytes\n", ext2.block_size);
        printf("  Inode size: %u bytes\n", ext2.inode_size);
        printf("  Total blocks: %u\n", ext2.sb.s_blocks_count);
        printf("  Free blocks: %u\n", ext2.sb.s_free_blocks_count);
        printf("  Total inodes: %u\n", ext2.sb.s_inodes_count);
        printf("  Free inodes: %u\n", ext2.sb.s_free_inodes_count);
    } else {
        printf("  Not mounted\n");
    }

    printf("\n[ext2] Statistics:\n");
    printf("  Blocks read: %llu\n", (unsigned long long)blocks_read);
    printf("  Inodes read: %llu\n", (unsigned long long)inodes_read);
    printf("  Directory lookups: %llu\n", (unsigned long long)dir_lookups);
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
    printf("  Ocean Ext2 Driver v%s\n", EXT2_VERSION);
    printf("========================================\n\n");

    printf("[ext2] PID: %d, PPID: %d\n", getpid(), getppid());

    ext2_init();
    ext2_serve();
    ext2_dump();

    printf("[ext2] Ext2 driver exiting\n");
    return 0;
}
