#define _POSIX_C_SOURCE 200809L

/* Timed DAC waveform output through the public libaioaio API.
 * One channel: one cosine period per waveform buffer.
 * Edit these settings, build, and run: axiodac_irq [DEVICE]
 */
#include "AIOAIO.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEFAULT_DEVICE "/dev/apci/mpcie_aio16_16fds_0"
#define DAC_CHANNELS 1u
#define WAVEFORM_SAMPLES (65536u * 4u)
#define POINT_RATE_HZ (1000000.0 / DAC_CHANNELS)
#ifndef DURATION_SECONDS
#define DURATION_SECONDS 5.0
#endif

static volatile sig_atomic_t interrupted;
static void on_signal(int number) { interrupted = number; }

static int failed(const char *operation, TWinErrCode error)
{
    if (error) fprintf(stderr, "%s: %s (%" PRIu32 ")\n", operation, AIO_ErrorString(error), error);
    return error != 0;
}

static int now_seconds(double *seconds)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) { perror("clock_gettime"); return 0; }
    *seconds = value.tv_sec + value.tv_nsec / 1000000000.0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *path = argc == 2 ? argv[1] : DEFAULT_DEVICE;
    TBoardIndex board = 0;
    TDACWaveformCapabilities caps = {.StructSize = sizeof(caps)};
    TDACWaveformStatus status = {.StructSize = sizeof(status)};
    static TUInt16 waveform[WAVEFORM_SAMPLES];
    struct sigaction action = {.sa_handler = on_signal};
    const struct timespec pause = {.tv_nsec = 100000000};
    TWinErrCode error;
    double start, now;
    int result = 1, started = 0;

    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        int help = argc == 2 && !strcmp(argv[1], "--help");
        fprintf(help ? stdout : stderr, "Usage: %s [DEVICE]\n"
                "One-channel cosine with IRQ refill for %.3g seconds or until Ctrl-C.\n"
                "Default device: %s\n"
                "Settings are constants in examples/axiodac_irq.c.\n",
                argv[0], DURATION_SECONDS, DEFAULT_DEVICE);
        return help ? 0 : 2;
    }

    /* 1. Open only the requested device and check timed DAC capabilities. */
    error = BRD_Open(path, &board);
    if (failed("BRD_Open", error)) {
        if (error == ENOENT || error == ENODEV || error == ENOTSUP)
            fprintf(stderr, "Check %s, that apci.ko is loaded, and that an AxIO FDS card is present.\n", path);
        if (error == EACCES || error == EPERM)
            fputs("Check read/write permissions on the APCI device node.\n", stderr);
        if (error == EBUSY) fputs("The APCI device is already in use.\n", stderr);
        return 1;
    }
    error = DAC_GetWaveformCapabilities(board, &caps);
    if (failed("DAC_GetWaveformCapabilities", error)) {
        if (error == ENOTSUP)
            fputs("Timed DAC output requires a supported AxIO FDS card; ADC-only cards are unsupported.\n", stderr);
        goto cleanup;
    }
    if (caps.ChannelCount < DAC_CHANNELS || POINT_RATE_HZ * DAC_CHANNELS > caps.MaxSampleRateHz) {
        fprintf(stderr, "Requested %u channels at %.9g points/s exceeds the board's "
                "%" PRIu32 " channels or %.9g scalar DAC samples/s maximum.\n",
                DAC_CHANNELS, POINT_RATE_HZ, caps.ChannelCount, caps.MaxSampleRateHz);
        goto cleanup;
    }

    /* 2. Supply interleaved 16-bit counts; the library adds channel tags. */
    for (TUInt32 point = 0; point < WAVEFORM_SAMPLES; ++point) {
        const double phase = 2.0 * 3.14159265358979323846 * point / WAVEFORM_SAMPLES;
        waveform[point] = (TUInt16)(32768.0 + 32767.0 * cos(phase));
    }
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction"); goto cleanup;
    }
    printf("One-channel cosine: %s; FPGA 0x%08" PRIx32 "; FIFO %" PRIu32 " samples.\n",
           path, caps.FirmwareRevision, caps.FIFOSamples);
    if (!now_seconds(&start)) goto cleanup;
    error = DAC_StartWaveform(board, DAC_CHANNELS, WAVEFORM_SAMPLES, waveform,
                              POINT_RATE_HZ, AIO_DAC_REFILL_IRQ);
    if (failed("DAC_StartWaveform", error)) goto cleanup;
    started = 1; /* Start copied waveform; the caller can now reuse its buffer. */
    if (failed("DAC_GetWaveformStatus", DAC_GetWaveformStatus(board, &status))) goto cleanup;
    printf("Output: %.9g points/s x %u channels = %.9g scalar samples/s, bipolar 10 V.\n"
           "IRQ refill; stopping after %.3g seconds or Ctrl-C.\n",
           status.ActualPointRateHz, DAC_CHANNELS, status.ActualPointRateHz * DAC_CHANNELS, DURATION_SECONDS);

    /* 3. The library owns the refill worker. Monitor only status and time. */
    for (;;) {
        if (interrupted) { result = 128 + interrupted; break; }
        if (failed("DAC_GetWaveformStatus", DAC_GetWaveformStatus(board, &status))) break;
        if (failed("DAC playback", status.ErrorCode)) break;
        if (status.State != AIO_STATE_RUNNING) { fputs("DAC playback stopped unexpectedly.\n", stderr); break; }
        if (!now_seconds(&now)) break;
        if (now - start >= DURATION_SECONDS) { result = 0; break; }
        if (nanosleep(&pause, NULL) && errno != EINTR) { perror("nanosleep"); break; }
    }

cleanup:
    /* 4. Stop joins the worker and restores zero volts, then close the board. */
    if (started) {
        error = DAC_StopWaveform(board);
        if (failed(error == status.ErrorCode ? "DAC_StopWaveform (earlier playback failure)" : "DAC_StopWaveform", error)) result = 1;
        if (failed("DAC_GetWaveformStatus", DAC_GetWaveformStatus(board, &status))) result = 1;
        else {
            if (status.ErrorCode && !result) result = 1;
            printf("Host uploads: %" PRIu64 " scalar DAC samples; refills: %" PRIu64
                   "; IRQ wakeups: %" PRIu64 ".\n",
                   status.UploadedSamples, status.Refills, status.IRQWakeups);
            puts("Counters describe host FIFO uploads, not measured physical playback.");
        }
    }
    error = BRD_Close(board);
    if (failed(error == status.ErrorCode ? "BRD_Close (earlier playback failure)" : "BRD_Close", error)) result = 1;
    return result;
}
