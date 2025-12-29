/*
 * Ocean Kernel - Programmable Interval Timer (PIT) Driver
 *
 * Sets up the 8254 PIT to provide timer interrupts for the scheduler.
 */

#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/sched.h>
#include "idt.h"

/* External functions */
extern int kprintf(const char *fmt, ...);

/*
 * 8254 PIT I/O Ports
 */
#define PIT_CHANNEL0    0x40    /* Channel 0 data port */
#define PIT_CHANNEL1    0x41    /* Channel 1 data port */
#define PIT_CHANNEL2    0x42    /* Channel 2 data port */
#define PIT_COMMAND     0x43    /* Mode/Command register */

/*
 * PIT Command Register bits
 *
 * Bits 7-6: Select Channel
 *   00 = Channel 0
 *   01 = Channel 1
 *   10 = Channel 2
 *   11 = Read-back command
 *
 * Bits 5-4: Access Mode
 *   00 = Latch count value command
 *   01 = Access mode: lobyte only
 *   10 = Access mode: hibyte only
 *   11 = Access mode: lobyte/hibyte
 *
 * Bits 3-1: Operating Mode
 *   000 = Mode 0 (interrupt on terminal count)
 *   001 = Mode 1 (hardware re-triggerable one-shot)
 *   010 = Mode 2 (rate generator)
 *   011 = Mode 3 (square wave generator)
 *   100 = Mode 4 (software triggered strobe)
 *   101 = Mode 5 (hardware triggered strobe)
 *
 * Bit 0: BCD/Binary mode
 *   0 = 16-bit binary
 *   1 = four-digit BCD
 */
#define PIT_CMD_CHANNEL0    0x00
#define PIT_CMD_LOHI        0x30    /* Access mode: lobyte/hibyte */
#define PIT_CMD_MODE2       0x04    /* Mode 2: rate generator */
#define PIT_CMD_MODE3       0x06    /* Mode 3: square wave */
#define PIT_CMD_BINARY      0x00    /* 16-bit binary mode */

/*
 * PIT Clock Frequency
 * The PIT's base frequency is approximately 1.193182 MHz
 */
#define PIT_FREQUENCY       1193182

/*
 * Timer tick counter
 */
static volatile u64 timer_ticks = 0;

/*
 * Timer interrupt handler
 *
 * Called on each PIT interrupt (HZ times per second)
 */
static void timer_interrupt_handler(struct trap_frame *frame)
{
    (void)frame;

    timer_ticks++;

    /* Call scheduler tick handler */
    sched_tick();
}

/*
 * Get current timer tick count
 */
u64 timer_get_ticks(void)
{
    return timer_ticks;
}

/*
 * Set the PIT frequency
 *
 * The divisor determines the frequency:
 *   frequency = PIT_FREQUENCY / divisor
 *
 * For HZ = 100:
 *   divisor = 1193182 / 100 = 11931
 */
static void pit_set_frequency(u32 hz)
{
    u16 divisor = (u16)(PIT_FREQUENCY / hz);

    /* Send the command byte */
    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_LOHI | PIT_CMD_MODE3 | PIT_CMD_BINARY);

    /* Send divisor (low byte then high byte) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

/*
 * Initialize the PIT timer
 */
void timer_init(void)
{
    kprintf("Initializing PIT timer...\n");

    /* Set the PIT to generate interrupts at HZ frequency */
    pit_set_frequency(HZ);
    kprintf("  PIT configured for %d Hz (divisor: %d)\n",
            HZ, PIT_FREQUENCY / HZ);

    /* Register our timer interrupt handler */
    irq_register(0, timer_interrupt_handler);

    /* Enable IRQ 0 (timer) on the PIC */
    pic_unmask_irq(0);

    kprintf("Timer initialized\n");
}

/*
 * Delay for a specified number of milliseconds
 * Uses busy-waiting (not efficient, but simple)
 */
void timer_delay_ms(u32 ms)
{
    u64 end = timer_ticks + (ms * HZ / 1000);
    while (timer_ticks < end) {
        __asm__ __volatile__("pause" ::: "memory");
    }
}

/*
 * Delay for a specified number of microseconds
 * Uses busy-waiting with PIT channel 2
 */
void timer_delay_us(u32 us)
{
    /* For short delays, just use a busy loop
     * This is approximate but works for calibration */
    u32 loops = us * 10;  /* Roughly 10 iterations per microsecond on modern CPUs */
    while (loops--) {
        __asm__ __volatile__("pause" ::: "memory");
    }
}
