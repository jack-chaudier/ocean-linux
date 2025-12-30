/*
 * Ocean ATA/IDE Driver
 *
 * Userspace driver for ATA/IDE disk controllers:
 *   - PIO mode data transfer (DMA support planned)
 *   - Primary and secondary channel support
 *   - LBA28/LBA48 addressing
 *   - Device identification
 *
 * NOTE: This driver requires port I/O syscalls which are
 * not yet implemented. For now, it simulates disk operations.
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define ATA_VERSION "0.1.0"
#define MAX_ATA_DEVICES 4

/* Port I/O simulation (until kernel provides port I/O syscalls) */
#define SIMULATED_IO 1

/* ATA device info */
struct ata_device {
    uint8_t  present;           /* Device present */
    uint8_t  channel;           /* 0 = primary, 1 = secondary */
    uint8_t  drive;             /* 0 = master, 1 = slave */
    uint8_t  atapi;             /* ATAPI device (CD-ROM) */
    uint8_t  lba48;             /* LBA48 support */
    uint64_t sectors;           /* Total sectors */
    uint16_t sector_size;       /* Bytes per sector */
    char     model[41];         /* Model string */
    char     serial[21];        /* Serial number */
    char     firmware[9];       /* Firmware version */
};

/* ATA channel info */
struct ata_channel {
    uint16_t io_base;           /* I/O base port */
    uint16_t ctrl_base;         /* Control base port */
    uint8_t  irq;               /* IRQ number */
    uint8_t  no_int;            /* Disable interrupts */
};

static struct ata_device ata_devices[MAX_ATA_DEVICES];
static struct ata_channel channels[2];
static int num_devices = 0;
static int ata_endpoint = -1;

/* Statistics */
static uint64_t sectors_read = 0;
static uint64_t sectors_written = 0;
static uint64_t errors = 0;

/*
 * Port I/O helpers (simulated until kernel provides syscalls)
 */
#if SIMULATED_IO

static uint8_t inb(uint16_t port)
{
    (void)port;
    /* Simulate device ready status */
    return ATA_SR_DRDY;
}

static uint16_t inw(uint16_t port)
{
    (void)port;
    return 0;
}

static void outb(uint16_t port, uint8_t value)
{
    (void)port;
    (void)value;
}

static void io_wait(void)
{
    /* Small delay - read status port 4 times */
    for (int i = 0; i < 4; i++) {
        inb(channels[0].ctrl_base);
    }
}

#else

/* Real port I/O using kernel syscalls */
static uint8_t inb(uint16_t port)
{
    /* TODO: sys_io_port_in(port, 1) */
    return 0;
}

static uint16_t inw(uint16_t port)
{
    /* TODO: sys_io_port_in(port, 2) */
    return 0;
}

static void outb(uint16_t port, uint8_t value)
{
    /* TODO: sys_io_port_out(port, value, 1) */
}

static void io_wait(void)
{
    inb(channels[0].ctrl_base);
    inb(channels[0].ctrl_base);
    inb(channels[0].ctrl_base);
    inb(channels[0].ctrl_base);
}

#endif

/*
 * Wait for BSY flag to clear
 */
static int ata_wait_bsy(struct ata_channel *ch)
{
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return ATA_ERR_TIMEOUT;
}

/*
 * Wait for DRQ (data request) flag
 */
static int ata_wait_drq(struct ata_channel *ch)
{
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return ATA_ERR_DEVICE;
        }
        if (status & ATA_SR_DF) {
            return ATA_ERR_DEVICE;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
    }
    return ATA_ERR_TIMEOUT;
}

/*
 * Select drive on channel
 */
static void ata_select_drive(struct ata_channel *ch, int drive)
{
    outb(ch->io_base + ATA_REG_DRIVE, 0xA0 | (drive << 4));
    io_wait();
}

/*
 * Software reset channel
 */
static void ata_soft_reset(struct ata_channel *ch)
{
    outb(ch->ctrl_base, 0x04);  /* Set SRST */
    io_wait();
    outb(ch->ctrl_base, 0x00);  /* Clear SRST */
    io_wait();
    ata_wait_bsy(ch);
}

/*
 * Identify device (ATA IDENTIFY command)
 */
static int ata_identify(struct ata_channel *ch, int drive, struct ata_device *dev)
{
    uint16_t identify_data[256];

    ata_select_drive(ch, drive);

    /* Clear sector count and LBA registers */
    outb(ch->io_base + ATA_REG_SECCOUNT, 0);
    outb(ch->io_base + ATA_REG_LBA_LO, 0);
    outb(ch->io_base + ATA_REG_LBA_MID, 0);
    outb(ch->io_base + ATA_REG_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    /* Check if device exists */
    uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
    if (status == 0) {
        return ATA_ERR_NODEV;  /* No device */
    }

    /* Wait for BSY to clear */
    if (ata_wait_bsy(ch) != 0) {
        return ATA_ERR_TIMEOUT;
    }

    /* Check for ATAPI */
    uint8_t lba_mid = inb(ch->io_base + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(ch->io_base + ATA_REG_LBA_HI);
    if (lba_mid != 0 || lba_hi != 0) {
        /* ATAPI or other device */
        dev->atapi = 1;
        return ATA_ERR_NODEV;  /* Skip ATAPI for now */
    }

    /* Wait for DRQ */
    if (ata_wait_drq(ch) != 0) {
        return ATA_ERR_TIMEOUT;
    }

    /* Read identify data */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ch->io_base + ATA_REG_DATA);
    }

    /* Parse identify data */
    dev->present = 1;
    dev->atapi = 0;

    /* Check LBA48 support (word 83, bit 10) */
    dev->lba48 = (identify_data[83] & (1 << 10)) ? 1 : 0;

    /* Get sector count */
    if (dev->lba48) {
        dev->sectors = ((uint64_t)identify_data[103] << 48) |
                       ((uint64_t)identify_data[102] << 32) |
                       ((uint64_t)identify_data[101] << 16) |
                       identify_data[100];
    } else {
        dev->sectors = ((uint32_t)identify_data[61] << 16) |
                       identify_data[60];
    }

    dev->sector_size = 512;

    /* Extract model string (words 27-46) */
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = identify_data[27 + i] >> 8;
        dev->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    dev->model[40] = '\0';

    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }

    /* Extract serial number (words 10-19) */
    for (int i = 0; i < 10; i++) {
        dev->serial[i * 2] = identify_data[10 + i] >> 8;
        dev->serial[i * 2 + 1] = identify_data[10 + i] & 0xFF;
    }
    dev->serial[20] = '\0';

    return ATA_OK;
}

/*
 * Read sectors (PIO mode)
 */
static int ata_read_sectors(struct ata_device *dev, uint64_t lba,
                            uint32_t count, void *buffer)
{
    if (!dev->present) {
        return ATA_ERR_NODEV;
    }

    struct ata_channel *ch = &channels[dev->channel];
    uint16_t *buf = (uint16_t *)buffer;

    ata_select_drive(ch, dev->drive);

    if (dev->lba48 && lba >= 0x10000000) {
        /* LBA48 mode */
        outb(ch->io_base + ATA_REG_SECCOUNT, (count >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, (lba >> 24) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 40) & 0xFF);

        outb(ch->io_base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

        outb(ch->io_base + ATA_REG_DRIVE, 0x40 | (dev->drive << 4));
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        /* LBA28 mode */
        outb(ch->io_base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(ch->io_base + ATA_REG_DRIVE, 0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F));
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    }

    /* Read data */
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_drq(ch) != 0) {
            errors++;
            return ATA_ERR_IO;
        }

        for (int i = 0; i < 256; i++) {
            *buf++ = inw(ch->io_base + ATA_REG_DATA);
        }
    }

    sectors_read += count;
    return ATA_OK;
}

/*
 * Write sectors (PIO mode)
 */
static int ata_write_sectors(struct ata_device *dev, uint64_t lba,
                             uint32_t count, const void *buffer)
{
    if (!dev->present) {
        return ATA_ERR_NODEV;
    }

    struct ata_channel *ch = &channels[dev->channel];
    const uint16_t *buf = (const uint16_t *)buffer;

    ata_select_drive(ch, dev->drive);

    if (dev->lba48 && lba >= 0x10000000) {
        /* LBA48 mode */
        outb(ch->io_base + ATA_REG_SECCOUNT, (count >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, (lba >> 24) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 40) & 0xFF);

        outb(ch->io_base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

        outb(ch->io_base + ATA_REG_DRIVE, 0x40 | (dev->drive << 4));
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    } else {
        /* LBA28 mode */
        outb(ch->io_base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(ch->io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(ch->io_base + ATA_REG_DRIVE, 0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F));
        outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    }

    /* Write data */
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_drq(ch) != 0) {
            errors++;
            return ATA_ERR_IO;
        }

        for (int i = 0; i < 256; i++) {
            /* outw not implemented, use two outb */
            outb(ch->io_base + ATA_REG_DATA, *buf & 0xFF);
            outb(ch->io_base + ATA_REG_DATA + 1, (*buf >> 8) & 0xFF);
            buf++;
        }
    }

    sectors_written += count;
    return ATA_OK;
}

/*
 * Probe for ATA devices
 */
static void ata_probe(void)
{
    printf("[ata] Probing for ATA devices...\n");

    /* Set up channel info */
    channels[0].io_base = ATA_PRIMARY_IO;
    channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    channels[0].irq = 14;

    channels[1].io_base = ATA_SECONDARY_IO;
    channels[1].ctrl_base = ATA_SECONDARY_CTRL;
    channels[1].irq = 15;

    /* Probe each channel */
    for (int ch = 0; ch < 2; ch++) {
        ata_soft_reset(&channels[ch]);

        for (int drv = 0; drv < 2; drv++) {
            int idx = ch * 2 + drv;
            struct ata_device *dev = &ata_devices[idx];

            dev->channel = ch;
            dev->drive = drv;

            int err = ata_identify(&channels[ch], drv, dev);
            if (err == ATA_OK && dev->present) {
                num_devices++;

                uint64_t size_mb = (dev->sectors * dev->sector_size) / (1024 * 1024);
                printf("[ata] Found device on %s %s:\n",
                       ch ? "secondary" : "primary",
                       drv ? "slave" : "master");
                printf("[ata]   Model: %s\n", dev->model);
                printf("[ata]   Size: %llu MB (%llu sectors)\n",
                       (unsigned long long)size_mb,
                       (unsigned long long)dev->sectors);
                printf("[ata]   LBA48: %s\n", dev->lba48 ? "yes" : "no");
            }
        }
    }

    if (num_devices == 0) {
        printf("[ata] No ATA devices found (simulated mode)\n");

        /* Create simulated device for testing */
        struct ata_device *dev = &ata_devices[0];
        dev->present = 1;
        dev->channel = 0;
        dev->drive = 0;
        dev->atapi = 0;
        dev->lba48 = 1;
        dev->sectors = 2097152;  /* 1 GB */
        dev->sector_size = 512;
        strncpy(dev->model, "QEMU HARDDISK (simulated)", sizeof(dev->model) - 1);
        strncpy(dev->serial, "QM00001", sizeof(dev->serial) - 1);
        num_devices = 1;

        printf("[ata] Created simulated device: %s\n", dev->model);
    }
}

/*
 * Initialize ATA driver
 */
static void ata_init(void)
{
    printf("[ata] ATA Driver v%s starting\n", ATA_VERSION);

    memset(ata_devices, 0, sizeof(ata_devices));

    ata_endpoint = endpoint_create(0);
    if (ata_endpoint < 0) {
        printf("[ata] Failed to create endpoint\n");
        return;
    }
    printf("[ata] Created endpoint %d\n", ata_endpoint);

    ata_probe();

    printf("[ata] ATA driver initialized\n");
}

/*
 * Service loop
 */
static void ata_serve(void)
{
    printf("[ata] Entering service loop\n");

    for (int i = 0; i < 50; i++) {
        yield();

        /* Self-test: read sector 0 */
        if (i == 10 && num_devices > 0) {
            uint8_t buffer[512];
            int err = ata_read_sectors(&ata_devices[0], 0, 1, buffer);
            if (err == ATA_OK) {
                printf("[ata] Self-test: read sector 0 OK\n");
            } else {
                printf("[ata] Self-test: read failed (%d)\n", err);
            }
        }

        /* Self-test: write sector 1000 */
        if (i == 20 && num_devices > 0) {
            uint8_t buffer[512];
            memset(buffer, 0x55, sizeof(buffer));
            int err = ata_write_sectors(&ata_devices[0], 1000, 1, buffer);
            if (err == ATA_OK) {
                printf("[ata] Self-test: write sector 1000 OK\n");
            } else {
                printf("[ata] Self-test: write failed (%d)\n", err);
            }
        }
    }
}

/*
 * Print driver info
 */
static void ata_dump(void)
{
    printf("\n[ata] ATA Devices:\n");
    printf("  CH  DRV  MODEL                     SIZE\n");
    printf("  --  ---  -----                     ----\n");

    for (int i = 0; i < MAX_ATA_DEVICES; i++) {
        if (ata_devices[i].present) {
            uint64_t size_mb = (ata_devices[i].sectors * ata_devices[i].sector_size) / (1024 * 1024);
            printf("  %d   %d    %-24s  %llu MB\n",
                   ata_devices[i].channel,
                   ata_devices[i].drive,
                   ata_devices[i].model,
                   (unsigned long long)size_mb);
        }
    }

    printf("\n[ata] Statistics:\n");
    printf("  Devices found: %d\n", num_devices);
    printf("  Sectors read: %llu\n", (unsigned long long)sectors_read);
    printf("  Sectors written: %llu\n", (unsigned long long)sectors_written);
    printf("  Errors: %llu\n", (unsigned long long)errors);
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
    printf("  Ocean ATA Driver v%s\n", ATA_VERSION);
    printf("========================================\n\n");

    printf("[ata] PID: %d, PPID: %d\n", getpid(), getppid());

    ata_init();
    ata_serve();
    ata_dump();

    printf("[ata] ATA driver exiting\n");
    return 0;
}
