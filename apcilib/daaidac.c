/*
 * mPCIe-DAAI16-8F immediate DAC output sample.
 *
 * Build: make daaidac
 * Run:   ./daaidac [/dev/apci/mpcie_daai16_8f_0]
 *
 * Sets all eight voltage outputs to the +/-10 V range, steps them from
 * -5 V to +5 V repeatedly until Ctrl-C, then returns initialized outputs
 * to 0 V. Outputs remain enabled after exit. Connect a voltmeter
 * between a DAC output and analog ground; this example uses voltage mode.
 * Run with exclusive access to the board (no other DAC application).
 * The sample resets both DAC chips, including their trim and slew registers.
 * ADC and digital I/O settings are left unchanged.
 *
 * These are software-paced, sequential channel updates, not a timed waveform
 * or simultaneous update. No FIFO, DMA, IRQ, or additional library is needed.
 * The DAAI's two AD5755-1 chips use a different command format from axiodac.c.
 * Register references: ACCES mPCIe-DAAI16-8F manual and AD5755-1 datasheet,
 * Rev. H, pages 30-37 (https://www.analog.com/media/en/technical-documentation/data-sheets/ad5755-1.pdf).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "apcilib.h"

#define DEVICEPATH             "/dev/apci/mpcie_daai16_8f_0"
#define DEVICE_ID              0xCEE8u
#define DEVICE_INDEX           0
#define BAR_REGISTER           1
#define DAC_CHANNELS           8u

/* FPGA registers, accessed through APCI's logical BAR numbering. */
#define DAC_OFFSET             0x04
#define DAC_RATE_OFFSET        0x08
#define DIO_CONTROL_OFFSET     0x48
#define DAC_SPI_BUSY           (1u << 31)
/* +48: external LDAC, DAC stream trigger, and external DAC clock enables. */
#define DAC_EXTERNAL_MODES     ((1u << 20) | (1u << 24) | (1u << 26))
#define DAC_TIMEOUT_NS         100000000L /* 100 ms */

/* AD5755-1 commands and DAC control-register fields. */
#define DAC_WRITE_DATA         0u
#define DAC_WRITE_CONTROL      7u
#define DAC_SOFTWARE_RESET     0x8555u
/* Internal compensation, in-phase converters, 410 kHz, 23 V maximum. */
#define DAC_DC_DC_CONFIG       0x6004u
#define DAC_CONTROL_SELECT     (2u << 13)
#define DAC_INTERNAL_ENABLE    (1u << 8)
#define DAC_OUTPUT_ENABLE      (1u << 6)
#define DAC_DC_DC_ENABLE       (1u << 4)
#define DAC_BIPOLAR_10V         3u
#define DAC_ZERO_VOLTS         0x8000u

static volatile sig_atomic_t terminate;

static void abort_handler(int signum)
{
    (void)signum;
    terminate = 1; /* All I/O and cleanup happen in main(), not the handler. */
}

/* Complete short hardware delays even if a signal interrupts nanosleep(). */
static int delay_ns(long ns)
{
    struct timespec delay = {0, ns};
    while (nanosleep(&delay, &delay) < 0)
        if (errno != EINTR)
            return -1;
    return 0;
}

static int wait_dac_ready(int fd)
{
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
        return -1;

    for (;;)
    {
        uint32_t status;
        if (apci_read32(fd, DEVICE_INDEX, BAR_REGISTER, DAC_OFFSET, &status) != 0)
            return -1;
        if (!(status & DAC_SPI_BUSY))
            return 0;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            return -1;
        if ((int64_t)(now.tv_sec - start.tv_sec) * 1000000000L +
            now.tv_nsec - start.tv_nsec >= DAC_TIMEOUT_NS)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        if (delay_ns(10000L) < 0)
            return -1;
    }
}

static int dac_command(int fd, unsigned int channel, uint32_t command, uint16_t data)
{
    /* Bits 22:21 select chip 1 (DAC0-3) or chip 2 (DAC4-7).
     * Bits 20:18 select the command; bits 17:16 select the chip's channel.
     * In internal-LDAC mode, each data write updates that output immediately.
     */
    uint32_t value = ((channel / 4u + 1u) << 21) | (command << 18) |
                     ((channel % 4u) << 16) | data;
    if (wait_dac_ready(fd) < 0)
        return -1;
    if (apci_write32(fd, DEVICE_INDEX, BAR_REGISTER, DAC_OFFSET, value) != 0)
        return -1;
    /* Busy includes the FPGA's inter-command delay as well as SPI transfer. */
    return wait_dac_ready(fd);
}

static int initialize_dac(int fd, unsigned int channel)
{
    const uint16_t control = DAC_CONTROL_SELECT | DAC_INTERNAL_ENABLE |
                             DAC_DC_DC_ENABLE | DAC_BIPOLAR_10V;

    /* Disable the output while selecting the range and powering its internal
     * circuitry. Preload zero, wait >=200 us, then enable the output.
     */
    if (dac_command(fd, channel, DAC_WRITE_CONTROL, control) < 0 ||
        dac_command(fd, channel, DAC_WRITE_DATA, DAC_ZERO_VOLTS) < 0 ||
        delay_ns(1000000L) < 0)
        return -1;
    return dac_command(fd, channel, DAC_WRITE_CONTROL, control | DAC_OUTPUT_ENABLE);
}

int main(int argc, char **argv)
{
    const char *device = argc == 2 ? argv[1] : DEVICEPATH;
    const uint16_t steps[] = {0x4000, 0x6000, 0x8000, 0xA000, 0xC000};
    struct sigaction action = {0};
    unsigned int device_id = 0, initialized = 0;
    uint32_t divisor, dio_control;
    int result = EXIT_FAILURE;

    if (argc > 2 || (argc == 2 &&
        (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))))
    {
        printf("Usage: %s [device-file]\nDefault: %s\n"
               "Repeats -5 V to +5 V steps on all eight DACs until Ctrl-C, then 0 V.\n",
               argv[0], DEVICEPATH);
        return argc > 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    action.sa_handler = abort_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0)
    {
        perror("Installing signal handlers");
        return EXIT_FAILURE;
    }

    int fd = open(device, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open %s: %s\n"
                "Check the device path, APCI driver, and device permissions.\n",
                device, strerror(errno));
        return EXIT_FAILURE;
    }
    if (apci_get_device_info(fd, DEVICE_INDEX, &device_id, NULL) != 0)
    {
        perror("Reading device information");
        goto cleanup;
    }
    if (device_id != DEVICE_ID)
    {
        fprintf(stderr, "Expected mPCIe-DAAI16-8F (0x%04X); found 0x%04X.\n",
                DEVICE_ID, device_id);
        goto cleanup;
    }

    /* Leave existing waveform/external-trigger setups alone. */
    if (apci_read32(fd, DEVICE_INDEX, BAR_REGISTER, DAC_RATE_OFFSET, &divisor) != 0 ||
        apci_read32(fd, DEVICE_INDEX, BAR_REGISTER, DIO_CONTROL_OFFSET, &dio_control) != 0)
    {
        perror("Reading DAC operating mode");
        goto cleanup;
    }
    if (divisor || (dio_control & DAC_EXTERNAL_MODES))
    {
        fprintf(stderr, "Stop timed/external DAC operation before running this sample.\n");
        goto cleanup;
    }

    printf("\nmPCIe-DAAI16-8F DAC sample: %s\n"
           "Resetting both DAC chips and configuring their supplies.\n", device);
    for (unsigned int channel = 0; channel < DAC_CHANNELS && !terminate; channel += 4)
    {
        /* These registers apply to a whole chip; address its first channel.
         * Wait >=100 us after reset before sending the DC-to-DC setup.
         */
        if (dac_command(fd, channel, DAC_WRITE_CONTROL, DAC_SOFTWARE_RESET) < 0 ||
            delay_ns(1000000L) < 0 ||
            dac_command(fd, channel, DAC_WRITE_CONTROL, DAC_DC_DC_CONFIG) < 0)
        {
            fprintf(stderr, "Setting up DAC chip %u: %s\n", channel / 4u, strerror(errno));
            goto cleanup;
        }
    }

    printf("Setting DAC0-7 to +/-10 V range, initially 0 V.\n");
    while (initialized < DAC_CHANNELS && !terminate)
    {
        if (initialize_dac(fd, initialized) < 0)
        {
            fprintf(stderr, "Initializing DAC%u: %s\n", initialized, strerror(errno));
            goto cleanup;
        }
        ++initialized;
    }

    printf("Repeating one second per step; Ctrl-C returns outputs to 0 V.\n");
    while (!terminate)
    {
        for (size_t step = 0; step < sizeof(steps) / sizeof(steps[0]) && !terminate; ++step)
        {
            for (unsigned int channel = 0; channel < DAC_CHANNELS && !terminate; ++channel)
            {
                if (dac_command(fd, channel, DAC_WRITE_DATA, steps[step]) < 0)
                {
                    fprintf(stderr, "Writing DAC%u: %s\n", channel, strerror(errno));
                    goto cleanup;
                }
            }
            if (!terminate)
            {
                /* Offset binary: 0 = -10 V, 32768 = 0 V, 65535 = +10 V - 1 LSB. */
                printf("DAC0-7: %+.2f V (0x%04X)\n",
                       steps[step] * 20.0 / 65536.0 - 10.0, (unsigned int)steps[step]);
                sleep(1);
            }
        }
    }
    result = EXIT_SUCCESS;

cleanup:
    /* Only fully initialized channels have a known range and enabled output.
     * Keep trying the other channels if one cleanup write fails.
     */
    for (unsigned int channel = 0; channel < initialized; ++channel)
    {
        if (dac_command(fd, channel, DAC_WRITE_DATA, DAC_ZERO_VOLTS) < 0)
        {
            fprintf(stderr, "Returning DAC%u to 0 V: %s\n", channel, strerror(errno));
            result = EXIT_FAILURE;
        }
    }
    if (close(fd) < 0)
    {
        perror("Closing device");
        result = EXIT_FAILURE;
    }
    if (result == EXIT_SUCCESS)
        printf("Sample done. Initialized outputs are at 0 V and remain enabled.\n");
    return result;
}
