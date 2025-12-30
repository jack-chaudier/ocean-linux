/*
 * Ocean Block Device Server
 *
 * Central multiplexer for block device I/O:
 *   - Device registration from drivers
 *   - Request routing to appropriate driver
 *   - Block caching (future)
 *   - Partition table parsing
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define BLK_VERSION "0.1.0"
#define MAX_DEVICES 16
#define MAX_PARTITIONS 64
#define SECTOR_SIZE 512

/* Block device entry */
struct block_device {
    uint32_t id;                /* Device ID */
    uint32_t type;              /* Device type */
    uint32_t flags;             /* Device flags */
    uint32_t driver_ep;         /* Driver endpoint */
    uint64_t total_blocks;      /* Total blocks */
    uint32_t block_size;        /* Block size */
    char     name[32];          /* Device name (e.g., "hda") */
    char     model[40];         /* Model string */
    char     serial[20];        /* Serial number */
};

/* Partition entry */
struct partition {
    uint32_t dev_id;            /* Parent device ID */
    uint32_t part_num;          /* Partition number */
    uint64_t start_block;       /* Start block on device */
    uint64_t num_blocks;        /* Number of blocks */
    uint8_t  type;              /* Partition type */
    uint8_t  bootable;          /* Bootable flag */
    char     name[32];          /* Partition name (e.g., "hda1") */
};

/* MBR partition entry structure */
struct mbr_partition {
    uint8_t  bootable;
    uint8_t  start_head;
    uint16_t start_cyl_sec;
    uint8_t  type;
    uint8_t  end_head;
    uint16_t end_cyl_sec;
    uint32_t start_lba;
    uint32_t num_sectors;
} __attribute__((packed));

static struct block_device devices[MAX_DEVICES];
static struct partition partitions[MAX_PARTITIONS];
static int num_devices = 0;
static int num_partitions = 0;
static int blk_endpoint = -1;
static uint32_t next_dev_id = 1;

/* Statistics */
static uint64_t read_requests = 0;
static uint64_t write_requests = 0;
static uint64_t blocks_read = 0;
static uint64_t blocks_written = 0;

/*
 * Find device by ID
 */
static struct block_device *find_device(uint32_t dev_id)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].id == dev_id && devices[i].flags & BLK_FLAG_PRESENT) {
            return &devices[i];
        }
    }
    return NULL;
}

/*
 * Find free device slot
 */
static struct block_device *alloc_device(void)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!(devices[i].flags & BLK_FLAG_PRESENT)) {
            return &devices[i];
        }
    }
    return NULL;
}

/*
 * Generate device name based on type and index
 */
static void generate_dev_name(struct block_device *dev, int index)
{
    const char *prefix;
    switch (dev->type) {
        case BLK_TYPE_ATA:    prefix = "hd"; break;
        case BLK_TYPE_VIRTIO: prefix = "vd"; break;
        case BLK_TYPE_NVME:   prefix = "nvme"; break;
        case BLK_TYPE_RAM:    prefix = "ram"; break;
        default:              prefix = "blk"; break;
    }

    if (dev->type == BLK_TYPE_NVME) {
        snprintf(dev->name, sizeof(dev->name), "%s%dn1", prefix, index);
    } else {
        /* hda, hdb, etc. or vda, vdb, etc. */
        dev->name[0] = prefix[0];
        dev->name[1] = prefix[1];
        dev->name[2] = 'a' + index;
        dev->name[3] = '\0';
    }
}

/*
 * Parse MBR partition table
 */
static int parse_mbr(struct block_device *dev, uint8_t *mbr_data)
{
    /* Check MBR signature */
    if (mbr_data[510] != 0x55 || mbr_data[511] != 0xAA) {
        printf("[blk] No valid MBR signature on %s\n", dev->name);
        return 0;
    }

    struct mbr_partition *parts = (struct mbr_partition *)(mbr_data + 446);
    int found = 0;

    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0) continue;  /* Empty partition */
        if (parts[i].num_sectors == 0) continue;

        if (num_partitions >= MAX_PARTITIONS) {
            printf("[blk] Partition table full\n");
            break;
        }

        struct partition *p = &partitions[num_partitions];
        p->dev_id = dev->id;
        p->part_num = i + 1;
        p->start_block = parts[i].start_lba;
        p->num_blocks = parts[i].num_sectors;
        p->type = parts[i].type;
        p->bootable = parts[i].bootable == 0x80;

        snprintf(p->name, sizeof(p->name), "%s%d", dev->name, i + 1);

        printf("[blk] Found partition %s: start=%llu, size=%llu blocks, type=0x%02x%s\n",
               p->name,
               (unsigned long long)p->start_block,
               (unsigned long long)p->num_blocks,
               p->type,
               p->bootable ? " (bootable)" : "");

        num_partitions++;
        found++;
    }

    return found;
}

/*
 * Handle BLK_REGISTER - register a new block device
 */
static int handle_register(uint32_t driver_ep, uint32_t type, uint32_t flags,
                           uint64_t total_blocks, uint32_t block_size,
                           const char *model, const char *serial,
                           uint32_t *out_dev_id)
{
    struct block_device *dev = alloc_device();
    if (!dev) {
        printf("[blk] No free device slots\n");
        return E_NOMEM;
    }

    dev->id = next_dev_id++;
    dev->type = type;
    dev->flags = flags | BLK_FLAG_PRESENT;
    dev->driver_ep = driver_ep;
    dev->total_blocks = total_blocks;
    dev->block_size = block_size;

    generate_dev_name(dev, num_devices);

    if (model) {
        strncpy(dev->model, model, sizeof(dev->model) - 1);
    }
    if (serial) {
        strncpy(dev->serial, serial, sizeof(dev->serial) - 1);
    }

    num_devices++;

    uint64_t size_mb = (total_blocks * block_size) / (1024 * 1024);
    printf("[blk] Registered device %s: %llu MB (%llu blocks x %u bytes)\n",
           dev->name, (unsigned long long)size_mb,
           (unsigned long long)total_blocks, block_size);

    if (model[0]) {
        printf("[blk]   Model: %s\n", dev->model);
    }

    *out_dev_id = dev->id;

    /* TODO: Read first sector and parse partition table */

    return E_OK;
}

/*
 * Handle BLK_READ - read blocks from device
 */
static int handle_read(uint32_t dev_id, uint64_t start_block,
                       uint32_t block_count, void *buffer,
                       uint32_t *blocks_done)
{
    read_requests++;

    struct block_device *dev = find_device(dev_id);
    if (!dev) {
        return E_NODEV;
    }

    /* Bounds check */
    if (start_block + block_count > dev->total_blocks) {
        return E_INVAL;
    }

    /* TODO: Send read request to driver via IPC
     * For now, simulate successful read
     */

    *blocks_done = block_count;
    blocks_read += block_count;

    return E_OK;
}

/*
 * Handle BLK_WRITE - write blocks to device
 */
static int handle_write(uint32_t dev_id, uint64_t start_block,
                        uint32_t block_count, const void *buffer,
                        uint32_t *blocks_done)
{
    write_requests++;

    struct block_device *dev = find_device(dev_id);
    if (!dev) {
        return E_NODEV;
    }

    /* Check readonly */
    if (dev->flags & BLK_FLAG_READONLY) {
        return E_PERM;
    }

    /* Bounds check */
    if (start_block + block_count > dev->total_blocks) {
        return E_INVAL;
    }

    /* TODO: Send write request to driver via IPC */

    *blocks_done = block_count;
    blocks_written += block_count;

    return E_OK;
}

/*
 * Handle BLK_GETINFO - get device information
 */
static int handle_getinfo(uint32_t dev_id, struct blk_getinfo_reply *info)
{
    struct block_device *dev = find_device(dev_id);
    if (!dev) {
        return E_NODEV;
    }

    info->type = dev->type;
    info->flags = dev->flags;
    info->total_blocks = dev->total_blocks;
    info->block_size = dev->block_size;
    strncpy(info->name, dev->name, sizeof(info->name));
    strncpy(info->model, dev->model, sizeof(info->model));
    strncpy(info->serial, dev->serial, sizeof(info->serial));

    return E_OK;
}

/*
 * Initialize block server
 */
static void blk_init(void)
{
    printf("[blk] Block Device Server v%s starting\n", BLK_VERSION);

    memset(devices, 0, sizeof(devices));
    memset(partitions, 0, sizeof(partitions));

    blk_endpoint = endpoint_create(0);
    if (blk_endpoint < 0) {
        printf("[blk] Failed to create endpoint\n");
        return;
    }
    printf("[blk] Created endpoint %d\n", blk_endpoint);

    printf("[blk] Block server initialized\n");
}

/*
 * Simulate device registration (for testing)
 */
static void simulate_devices(void)
{
    printf("[blk] Simulating device registration...\n");

    /* Simulate an ATA disk */
    uint32_t dev_id;
    int err = handle_register(
        100,                    /* Driver endpoint */
        BLK_TYPE_ATA,           /* Type */
        0,                      /* Flags */
        2097152,                /* 1GB in 512-byte blocks */
        512,                    /* Block size */
        "QEMU HARDDISK",        /* Model */
        "QM00001",              /* Serial */
        &dev_id
    );

    if (err == E_OK) {
        printf("[blk] Simulated ATA disk registered as device %u\n", dev_id);
    }

    /* Simulate a VirtIO disk */
    err = handle_register(
        101,                    /* Driver endpoint */
        BLK_TYPE_VIRTIO,        /* Type */
        0,                      /* Flags */
        4194304,                /* 2GB in 512-byte blocks */
        512,                    /* Block size */
        "VirtIO Block Device",  /* Model */
        "VIRTIO-001",           /* Serial */
        &dev_id
    );

    if (err == E_OK) {
        printf("[blk] Simulated VirtIO disk registered as device %u\n", dev_id);
    }
}

/*
 * Service loop
 */
static void blk_serve(void)
{
    printf("[blk] Entering service loop\n");

    /* Simulate some devices for testing */
    simulate_devices();

    /* Simulate some I/O operations */
    for (int i = 0; i < 50; i++) {
        yield();

        /* Simulate read request */
        if (i == 10) {
            uint32_t done;
            uint8_t buffer[512];
            int err = handle_read(1, 0, 1, buffer, &done);
            if (err == E_OK) {
                printf("[blk] Self-test: read %u blocks\n", done);
            }
        }

        /* Simulate write request */
        if (i == 20) {
            uint32_t done;
            uint8_t buffer[512];
            memset(buffer, 0xAA, sizeof(buffer));
            int err = handle_write(1, 100, 1, buffer, &done);
            if (err == E_OK) {
                printf("[blk] Self-test: wrote %u blocks\n", done);
            }
        }

        /* Get device info */
        if (i == 30) {
            struct blk_getinfo_reply info;
            int err = handle_getinfo(1, &info);
            if (err == E_OK) {
                printf("[blk] Self-test: device info - %s, %llu blocks\n",
                       info.name, (unsigned long long)info.total_blocks);
            }
        }
    }
}

/*
 * Print device list
 */
static void blk_dump(void)
{
    printf("\n[blk] Block Devices:\n");
    printf("  ID  NAME   TYPE     SIZE        MODEL\n");
    printf("  --  ----   ----     ----        -----\n");

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].flags & BLK_FLAG_PRESENT) {
            const char *type_str;
            switch (devices[i].type) {
                case BLK_TYPE_ATA:    type_str = "ATA";    break;
                case BLK_TYPE_VIRTIO: type_str = "VirtIO"; break;
                case BLK_TYPE_NVME:   type_str = "NVMe";   break;
                case BLK_TYPE_RAM:    type_str = "RAM";    break;
                default:              type_str = "???";    break;
            }

            uint64_t size_mb = (devices[i].total_blocks * devices[i].block_size) / (1024 * 1024);
            printf("  %-2u  %-5s  %-6s  %4llu MB    %s\n",
                   devices[i].id,
                   devices[i].name,
                   type_str,
                   (unsigned long long)size_mb,
                   devices[i].model);
        }
    }

    if (num_partitions > 0) {
        printf("\n[blk] Partitions:\n");
        printf("  NAME    START        SIZE         TYPE\n");
        printf("  ----    -----        ----         ----\n");

        for (int i = 0; i < num_partitions; i++) {
            printf("  %-6s  %-10llu  %-10llu  0x%02x\n",
                   partitions[i].name,
                   (unsigned long long)partitions[i].start_block,
                   (unsigned long long)partitions[i].num_blocks,
                   partitions[i].type);
        }
    }

    printf("\n[blk] Statistics:\n");
    printf("  Devices: %d\n", num_devices);
    printf("  Partitions: %d\n", num_partitions);
    printf("  Read requests: %llu (%llu blocks)\n",
           (unsigned long long)read_requests,
           (unsigned long long)blocks_read);
    printf("  Write requests: %llu (%llu blocks)\n",
           (unsigned long long)write_requests,
           (unsigned long long)blocks_written);
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
    printf("  Ocean Block Device Server v%s\n", BLK_VERSION);
    printf("========================================\n\n");

    printf("[blk] PID: %d, PPID: %d\n", getpid(), getppid());

    blk_init();
    blk_serve();
    blk_dump();

    printf("[blk] Block server exiting\n");
    return 0;
}
