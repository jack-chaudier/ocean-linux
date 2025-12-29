/*
 * Ocean Kernel - Early Serial Console
 *
 * Provides serial port output for early boot debugging.
 * Uses COM1 (0x3F8) with 115200 baud, 8N1.
 */

#include <ocean/types.h>
#include <ocean/defs.h>

/* Serial port addresses */
#define COM1_PORT           0x3F8
#define COM2_PORT           0x2F8
#define COM3_PORT           0x3E8
#define COM4_PORT           0x2E8

/* Serial port register offsets */
#define SERIAL_DATA         0   /* Data register (R/W) */
#define SERIAL_IER          1   /* Interrupt Enable Register */
#define SERIAL_FCR          2   /* FIFO Control Register (W) */
#define SERIAL_IIR          2   /* Interrupt ID Register (R) */
#define SERIAL_LCR          3   /* Line Control Register */
#define SERIAL_MCR          4   /* Modem Control Register */
#define SERIAL_LSR          5   /* Line Status Register */
#define SERIAL_MSR          6   /* Modem Status Register */
#define SERIAL_SCRATCH      7   /* Scratch Register */

/* Divisor latch registers (when DLAB is set) */
#define SERIAL_DLL          0   /* Divisor Latch Low */
#define SERIAL_DLH          1   /* Divisor Latch High */

/* Line Control Register bits */
#define LCR_DLAB            0x80    /* Divisor Latch Access Bit */
#define LCR_BREAK           0x40    /* Break Enable */
#define LCR_PARITY_MASK     0x38    /* Parity bits mask */
#define LCR_PARITY_NONE     0x00    /* No parity */
#define LCR_PARITY_ODD      0x08    /* Odd parity */
#define LCR_PARITY_EVEN     0x18    /* Even parity */
#define LCR_STOP_BITS       0x04    /* 2 stop bits (0 = 1 stop bit) */
#define LCR_WORD_LEN_MASK   0x03    /* Word length mask */
#define LCR_WORD_LEN_5      0x00    /* 5 bits */
#define LCR_WORD_LEN_6      0x01    /* 6 bits */
#define LCR_WORD_LEN_7      0x02    /* 7 bits */
#define LCR_WORD_LEN_8      0x03    /* 8 bits */

/* Line Status Register bits */
#define LSR_DATA_READY      0x01    /* Data ready */
#define LSR_OVERRUN         0x02    /* Overrun error */
#define LSR_PARITY_ERR      0x04    /* Parity error */
#define LSR_FRAMING_ERR     0x08    /* Framing error */
#define LSR_BREAK_INT       0x10    /* Break interrupt */
#define LSR_THRE            0x20    /* Transmitter holding register empty */
#define LSR_TEMT            0x40    /* Transmitter empty */
#define LSR_FIFO_ERR        0x80    /* FIFO error */

/* FIFO Control Register bits */
#define FCR_ENABLE          0x01    /* Enable FIFOs */
#define FCR_CLEAR_RX        0x02    /* Clear receive FIFO */
#define FCR_CLEAR_TX        0x04    /* Clear transmit FIFO */
#define FCR_DMA_MODE        0x08    /* DMA mode select */
#define FCR_TRIGGER_1       0x00    /* 1 byte trigger */
#define FCR_TRIGGER_4       0x40    /* 4 byte trigger */
#define FCR_TRIGGER_8       0x80    /* 8 byte trigger */
#define FCR_TRIGGER_14      0xC0    /* 14 byte trigger */

/* Modem Control Register bits */
#define MCR_DTR             0x01    /* Data Terminal Ready */
#define MCR_RTS             0x02    /* Request To Send */
#define MCR_OUT1            0x04    /* Output 1 */
#define MCR_OUT2            0x08    /* Output 2 (enables IRQ) */
#define MCR_LOOPBACK        0x10    /* Loopback mode */

/* Baud rate divisors (for 115200 base) */
#define BAUD_115200         1
#define BAUD_57600          2
#define BAUD_38400          3
#define BAUD_19200          6
#define BAUD_9600           12

/* Currently active serial port */
static u16 serial_port = 0;

/*
 * Wait until transmitter is ready
 */
static void serial_wait_tx_ready(void)
{
    /* Wait for transmitter holding register to be empty */
    while ((inb(serial_port + SERIAL_LSR) & LSR_THRE) == 0) {
        cpu_pause();
    }
}

/*
 * Initialize serial port
 *
 * Returns: true if serial port is present, false otherwise
 */
bool serial_init(u16 port, u32 baud)
{
    serial_port = port;

    /* Calculate divisor */
    u16 divisor;
    switch (baud) {
    case 115200: divisor = BAUD_115200; break;
    case 57600:  divisor = BAUD_57600; break;
    case 38400:  divisor = BAUD_38400; break;
    case 19200:  divisor = BAUD_19200; break;
    case 9600:   divisor = BAUD_9600; break;
    default:     divisor = BAUD_115200; break;
    }

    /* Disable interrupts */
    outb(port + SERIAL_IER, 0x00);

    /* Enable DLAB to set baud rate divisor */
    outb(port + SERIAL_LCR, LCR_DLAB);

    /* Set divisor */
    outb(port + SERIAL_DLL, divisor & 0xFF);
    outb(port + SERIAL_DLH, (divisor >> 8) & 0xFF);

    /* 8 bits, no parity, 1 stop bit (8N1), disable DLAB */
    outb(port + SERIAL_LCR, LCR_WORD_LEN_8 | LCR_PARITY_NONE);

    /* Enable and clear FIFOs, set 14-byte threshold */
    outb(port + SERIAL_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX |
                            FCR_TRIGGER_14);

    /* Set RTS and DTR, enable IRQ */
    outb(port + SERIAL_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Test serial port by checking loopback */
    outb(port + SERIAL_MCR, MCR_LOOPBACK | MCR_OUT1 | MCR_OUT2);

    /* Send a test byte */
    outb(port + SERIAL_DATA, 0xAE);

    /* Check if we receive it back */
    if (inb(port + SERIAL_DATA) != 0xAE) {
        serial_port = 0;
        return false;
    }

    /* Disable loopback, restore normal operation */
    outb(port + SERIAL_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    return true;
}

/*
 * Initialize default serial port (COM1, 115200 baud)
 */
void serial_early_init(void)
{
    /* Try COM1 first */
    if (serial_init(COM1_PORT, 115200)) {
        return;
    }

    /* Try COM2 as fallback */
    if (serial_init(COM2_PORT, 115200)) {
        return;
    }

    /* No serial port available */
    serial_port = 0;
}

/*
 * Output a single character to serial port
 */
void serial_putc(char c)
{
    if (serial_port == 0) {
        return;
    }

    /* Handle newline -> CRLF */
    if (c == '\n') {
        serial_wait_tx_ready();
        outb(serial_port + SERIAL_DATA, '\r');
    }

    serial_wait_tx_ready();
    outb(serial_port + SERIAL_DATA, c);
}

/*
 * Output a string to serial port
 */
void serial_puts(const char *s)
{
    while (*s) {
        serial_putc(*s++);
    }
}

/*
 * Read a character from serial port (blocking)
 */
int serial_getc(void)
{
    if (serial_port == 0) {
        return -1;
    }

    /* Wait for data */
    while ((inb(serial_port + SERIAL_LSR) & LSR_DATA_READY) == 0) {
        cpu_pause();
    }

    return inb(serial_port + SERIAL_DATA);
}

/*
 * Check if data is available to read
 */
bool serial_data_available(void)
{
    if (serial_port == 0) {
        return false;
    }

    return (inb(serial_port + SERIAL_LSR) & LSR_DATA_READY) != 0;
}

/*
 * Get current serial port address
 */
u16 serial_get_port(void)
{
    return serial_port;
}
