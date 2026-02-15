/*
 * Ocean IPC Protocol Definitions
 *
 * Common message types and structures for inter-server communication.
 */

#ifndef _OCEAN_IPC_PROTO_H
#define _OCEAN_IPC_PROTO_H

#include <stdint.h>

#define IPC_PROTO_VERSION 2

/*
 * Message tag format (64 bits):
 *   [63:44] Label      - User-defined message type (20 bits)
 *   [43:38] Length     - Number of data words (6 bits)
 *   [37:34] Cap Count  - Capabilities transferred (4 bits)
 *   [33:26] Flags      - Operation flags (8 bits)
 *   [25:10] Error      - Error code for replies (16 bits)
 *   [9:0]   Reserved
 */

#define IPC_TAG_LABEL_SHIFT     44
#define IPC_TAG_LABEL_MASK      0xFFFFF
#define IPC_TAG_LENGTH_SHIFT    38
#define IPC_TAG_LENGTH_MASK     0x3F
#define IPC_TAG_CAPS_SHIFT      34
#define IPC_TAG_CAPS_MASK       0xF
#define IPC_TAG_FLAGS_SHIFT     26
#define IPC_TAG_FLAGS_MASK      0xFF
#define IPC_TAG_ERROR_SHIFT     10
#define IPC_TAG_ERROR_MASK      0xFFFF

#define IPC_MAKE_TAG(label, len, caps, flags) \
    (((uint64_t)(label) << IPC_TAG_LABEL_SHIFT) | \
     ((uint64_t)(len) << IPC_TAG_LENGTH_SHIFT) | \
     ((uint64_t)(caps) << IPC_TAG_CAPS_SHIFT) | \
     ((uint64_t)(flags) << IPC_TAG_FLAGS_SHIFT))

#define IPC_TAG_LABEL(tag)  (((tag) >> IPC_TAG_LABEL_SHIFT) & IPC_TAG_LABEL_MASK)
#define IPC_TAG_LENGTH(tag) (((tag) >> IPC_TAG_LENGTH_SHIFT) & IPC_TAG_LENGTH_MASK)
#define IPC_TAG_CAPS(tag)   (((tag) >> IPC_TAG_CAPS_SHIFT) & IPC_TAG_CAPS_MASK)
#define IPC_TAG_FLAGS(tag)  (((tag) >> IPC_TAG_FLAGS_SHIFT) & IPC_TAG_FLAGS_MASK)
#define IPC_TAG_ERROR(tag)  (((tag) >> IPC_TAG_ERROR_SHIFT) & IPC_TAG_ERROR_MASK)

/* Tag flags */
#define IPC_FLAG_REPLY      (1 << 0)    /* This is a reply */
#define IPC_FLAG_ERROR      (1 << 1)    /* Error response */

/*
 * Well-known endpoint IDs
 */
#define EP_INIT         1       /* Init server */
#define EP_MEM          2       /* Memory server */
#define EP_PROC         3       /* Process server */
#define EP_VFS          4       /* VFS server */
#define EP_BLK          5       /* Block device server */
#define EP_TTY          6       /* TTY server */
#define EP_RS           7       /* Reincarnation server */

/*
 * Error codes
 */
#define E_OK            0       /* Success */
#define E_INVAL         1       /* Invalid argument */
#define E_NOMEM         2       /* Out of memory */
#define E_NOENT         3       /* No such entry */
#define E_BUSY          4       /* Resource busy */
#define E_PERM          5       /* Permission denied */
#define E_IO            6       /* I/O error */
#define E_NOSYS         7       /* Not implemented */
#define E_FAULT         8       /* Bad address */
#define E_EXIST         9       /* Already exists */
#define E_NODEV         10      /* No such device */

/*
 * Memory Server Protocol
 */

/* Message labels */
#define MEM_ALLOC_PHYS      0x100   /* Allocate physical pages */
#define MEM_FREE_PHYS       0x101   /* Free physical pages */
#define MEM_MAP             0x102   /* Map memory region */
#define MEM_UNMAP           0x103   /* Unmap memory region */
#define MEM_GRANT           0x104   /* Grant memory to another process */
#define MEM_QUERY           0x105   /* Query memory info */

/* MEM_ALLOC_PHYS request */
struct mem_alloc_req {
    uint64_t pages;         /* Number of pages */
    uint64_t flags;         /* Allocation flags */
};

/* MEM_ALLOC_PHYS reply */
struct mem_alloc_reply {
    uint64_t phys_addr;     /* Physical address */
    uint64_t pages;         /* Pages allocated */
};

/* MEM_MAP request */
struct mem_map_req {
    uint64_t virt_addr;     /* Virtual address (0 = any) */
    uint64_t phys_addr;     /* Physical address (0 = allocate) */
    uint64_t pages;         /* Number of pages */
    uint64_t flags;         /* Mapping flags */
};

/* MEM_MAP reply */
struct mem_map_reply {
    uint64_t virt_addr;     /* Mapped virtual address */
    uint64_t pages;         /* Pages mapped */
};

/*
 * Process Server Protocol
 */

/* Message labels */
#define PROC_SPAWN          0x200   /* Spawn new process */
#define PROC_EXIT           0x201   /* Process exit notification */
#define PROC_WAIT           0x202   /* Wait for process */
#define PROC_KILL           0x203   /* Kill process */
#define PROC_GETINFO        0x204   /* Get process info */
#define PROC_SETINFO        0x205   /* Set process info */
#define PROC_FORK           0x206   /* Fork process */
#define PROC_EXEC           0x207   /* Execute new program */

/* PROC_SPAWN request */
struct proc_spawn_req {
    uint64_t path_ptr;      /* Path to executable */
    uint64_t path_len;      /* Path length */
    uint64_t argv_ptr;      /* Arguments */
    uint64_t envp_ptr;      /* Environment */
};

/* PROC_SPAWN reply */
struct proc_spawn_reply {
    uint64_t pid;           /* New process ID */
    uint64_t endpoint;      /* Process endpoint */
};

/* PROC_WAIT request */
struct proc_wait_req {
    uint64_t pid;           /* PID to wait for (-1 = any) */
    uint64_t flags;         /* Wait flags */
};

/* PROC_WAIT reply */
struct proc_wait_reply {
    uint64_t pid;           /* PID that exited */
    uint64_t status;        /* Exit status */
};

/*
 * VFS Server Protocol
 */

/* Message labels */
#define VFS_OPEN            0x300   /* Open file */
#define VFS_CLOSE           0x301   /* Close file */
#define VFS_READ            0x302   /* Read from file */
#define VFS_WRITE           0x303   /* Write to file */
#define VFS_LSEEK           0x304   /* Seek in file */
#define VFS_STAT            0x305   /* Get file status */
#define VFS_FSTAT           0x306   /* Get file status by fd */
#define VFS_MKDIR           0x307   /* Create directory */
#define VFS_RMDIR           0x308   /* Remove directory */
#define VFS_UNLINK          0x309   /* Remove file */
#define VFS_RENAME          0x30A   /* Rename file */
#define VFS_READDIR         0x30B   /* Read directory */
#define VFS_MOUNT           0x30C   /* Mount filesystem */
#define VFS_UMOUNT          0x30D   /* Unmount filesystem */
#define VFS_CHDIR           0x30E   /* Change directory */
#define VFS_GETCWD          0x30F   /* Get working directory */

/* VFS_OPEN request */
struct vfs_open_req {
    uint64_t path_ptr;      /* Path to file */
    uint64_t path_len;      /* Path length */
    uint64_t flags;         /* Open flags */
    uint64_t mode;          /* File mode (for create) */
};

/* VFS_OPEN reply */
struct vfs_open_reply {
    uint64_t fd;            /* File descriptor */
};

/* VFS_READ/VFS_WRITE request */
struct vfs_io_req {
    uint64_t fd;            /* File descriptor */
    uint64_t buf_ptr;       /* Buffer pointer */
    uint64_t count;         /* Bytes to read/write */
};

/* VFS_READ/VFS_WRITE reply */
struct vfs_io_reply {
    uint64_t bytes;         /* Bytes read/written */
};

/* VFS_LSEEK request */
struct vfs_lseek_req {
    uint64_t fd;            /* File descriptor */
    uint64_t offset;        /* Offset */
    uint64_t whence;        /* SEEK_SET, SEEK_CUR, SEEK_END */
};

/* VFS_LSEEK reply */
struct vfs_lseek_reply {
    uint64_t position;      /* New file position */
};

/* VFS_STAT result */
struct vfs_stat {
    uint64_t mode;          /* File mode */
    uint64_t size;          /* File size */
    uint64_t nlink;         /* Number of links */
    uint64_t uid;           /* Owner user ID */
    uint64_t gid;           /* Owner group ID */
    uint64_t atime;         /* Access time */
    uint64_t mtime;         /* Modification time */
    uint64_t ctime;         /* Status change time */
};

/* VFS_READDIR entry */
struct vfs_dirent {
    uint64_t ino;           /* Inode number */
    uint16_t reclen;        /* Record length */
    uint8_t  type;          /* File type */
    uint8_t  namelen;       /* Name length */
    char     name[256];     /* File name */
};

/* File types (for mode field) */
#define S_IFREG     0100000     /* Regular file */
#define S_IFDIR     0040000     /* Directory */
#define S_IFLNK     0120000     /* Symbolic link */
#define S_IFBLK     0060000     /* Block device */
#define S_IFCHR     0020000     /* Character device */
#define S_IFIFO     0010000     /* FIFO (named pipe) */
#define S_IFSOCK    0140000     /* Socket */

#define S_IFMT      0170000     /* File type mask */

/* Permission bits */
#define S_ISUID     04000       /* Set UID on execution */
#define S_ISGID     02000       /* Set GID on execution */
#define S_ISVTX     01000       /* Sticky bit */

#define S_IRUSR     00400       /* Owner read */
#define S_IWUSR     00200       /* Owner write */
#define S_IXUSR     00100       /* Owner execute */
#define S_IRGRP     00040       /* Group read */
#define S_IWGRP     00020       /* Group write */
#define S_IXGRP     00010       /* Group execute */
#define S_IROTH     00004       /* Others read */
#define S_IWOTH     00002       /* Others write */
#define S_IXOTH     00001       /* Others execute */

/* Type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/*
 * FS Driver Protocol (VFS <-> filesystem drivers)
 */

/* Message labels */
#define FS_READSUPER        0x400   /* Read superblock/mount */
#define FS_UNMOUNT          0x401   /* Unmount */
#define FS_LOOKUP           0x402   /* Lookup path component */
#define FS_CREATE           0x403   /* Create file */
#define FS_MKDIR            0x404   /* Create directory */
#define FS_UNLINK           0x405   /* Remove entry */
#define FS_RENAME           0x406   /* Rename entry */
#define FS_READ             0x407   /* Read file data */
#define FS_WRITE            0x408   /* Write file data */
#define FS_GETDENTS         0x409   /* Read directory */
#define FS_STAT             0x40A   /* Get inode status */
#define FS_CHMOD            0x40B   /* Change mode */
#define FS_CHOWN            0x40C   /* Change owner */
#define FS_TRUNC            0x40D   /* Truncate file */
#define FS_SYNC             0x40E   /* Sync to disk */

/*
 * Block Device Protocol (BLK server <-> drivers)
 */

/* Message labels */
#define BLK_REGISTER        0x500   /* Register block device */
#define BLK_UNREGISTER      0x501   /* Unregister block device */
#define BLK_READ            0x502   /* Read blocks */
#define BLK_WRITE           0x503   /* Write blocks */
#define BLK_FLUSH           0x504   /* Flush to disk */
#define BLK_GETINFO         0x505   /* Get device info */
#define BLK_IOCTL           0x506   /* Device-specific control */

/* Block device types */
#define BLK_TYPE_UNKNOWN    0
#define BLK_TYPE_ATA        1       /* ATA/IDE disk */
#define BLK_TYPE_VIRTIO     2       /* VirtIO block device */
#define BLK_TYPE_NVME       3       /* NVMe device */
#define BLK_TYPE_RAM        4       /* RAM disk */

/* Block device flags */
#define BLK_FLAG_READONLY   (1 << 0)
#define BLK_FLAG_REMOVABLE  (1 << 1)
#define BLK_FLAG_PRESENT    (1 << 2)

/* BLK_REGISTER request */
struct blk_register_req {
    uint32_t type;              /* Device type */
    uint32_t flags;             /* Device flags */
    uint64_t total_blocks;      /* Total blocks */
    uint32_t block_size;        /* Block size in bytes */
    char     name[32];          /* Device name */
};

/* BLK_REGISTER reply */
struct blk_register_reply {
    uint32_t dev_id;            /* Assigned device ID */
};

/* BLK_READ/BLK_WRITE request */
struct blk_io_req {
    uint32_t dev_id;            /* Device ID */
    uint64_t start_block;       /* Starting block number */
    uint32_t block_count;       /* Number of blocks */
    uint64_t buffer_ptr;        /* Buffer address */
};

/* BLK_IO reply */
struct blk_io_reply {
    uint32_t blocks_done;       /* Blocks actually transferred */
    uint32_t error;             /* Error code (0 = success) */
};

/* BLK_GETINFO request */
struct blk_getinfo_req {
    uint32_t dev_id;            /* Device ID */
};

/* BLK_GETINFO reply */
struct blk_getinfo_reply {
    uint32_t type;              /* Device type */
    uint32_t flags;             /* Device flags */
    uint64_t total_blocks;      /* Total blocks */
    uint32_t block_size;        /* Block size */
    char     name[32];          /* Device name */
    char     model[40];         /* Model string */
    char     serial[20];        /* Serial number */
};

/*
 * ATA Driver Protocol
 */

/* ATA command results */
#define ATA_OK              0
#define ATA_ERR_TIMEOUT     1
#define ATA_ERR_DEVICE      2
#define ATA_ERR_IO          3
#define ATA_ERR_NODEV       4

/* ATA port I/O addresses */
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

/* ATA registers (offset from base) */
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE       0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

/* ATA commands */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_FLUSH       0xE7
#define ATA_CMD_FLUSH_EXT   0xEA

/* ATA status bits */
#define ATA_SR_BSY          0x80    /* Busy */
#define ATA_SR_DRDY         0x40    /* Drive ready */
#define ATA_SR_DF           0x20    /* Drive fault */
#define ATA_SR_DSC          0x10    /* Drive seek complete */
#define ATA_SR_DRQ          0x08    /* Data request */
#define ATA_SR_CORR         0x04    /* Corrected data */
#define ATA_SR_IDX          0x02    /* Index */
#define ATA_SR_ERR          0x01    /* Error */

#endif /* _OCEAN_IPC_PROTO_H */
