#define _POSIX_C_SOURCE 200809L

/* Minimal finite ADC DMA capture using only the public libaioaio API.
 * Adjust these settings, build, and run: check_dma [DEVICE]
 * The rate is shared by the selected channels on each physical ADC.
 */
#include "AIOAIO.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef CAPTURE_SAMPLES
#define CAPTURE_SAMPLES 100000u
#endif
#ifndef ADC_RATE_HZ
#define ADC_RATE_HZ 10000.0
#endif
#ifndef TIMEOUT_SECONDS
#define TIMEOUT_SECONDS 30.0
#endif

enum { BUFFER_SAMPLES = 4096, PREVIEW_SAMPLES = 8 };
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
    TBoardIndex board = 0;
    TBoardCapabilities caps = {.StructSize = sizeof(caps)};
    TADCAcquisitionStatus status = {.StructSize = sizeof(status)};
    TUInt32 buffer[BUFFER_SAMPLES], preview[PREVIEW_SAMPLES], delivered = 0, shown = 0;
    TWinErrCode error;
    double actual_rate, start, now;
    int result = 1, started = 0;
    struct sigaction action = {.sa_handler = on_signal};

    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        int help = argc == 2 && !strcmp(argv[1], "--help");
        fprintf(help ? stdout : stderr, "Usage: %s [DEVICE]\n"
                "Capture %u ADC results at %.9g conversions/s/ADC.\n"
                "Edit the constants in examples/check_dma.c to change settings.\n"
                "Use axio_benchmark for configurable captures, CSV, and benchmarks.\n",
                argv[0], CAPTURE_SAMPLES, ADC_RATE_HZ);
        return help ? 0 : 2;
    }

    /* 1. Open an exact device, or discover boards and select the first one. */
    error = argc == 2 ? BRD_Open(argv[1], &board) : AIO_Initialize();
    if (failed(argc == 2 ? "BRD_Open" : "AIO_Initialize", error)) {
        if (error == ENOENT || error == ENODEV || error == ENOTSUP)
            fputs("Check the device path, that the APCI driver (apci.ko) is loaded, "
                  "and that a supported AxIO ADC card is detected under /dev/apci.\n", stderr);
        if (error == EACCES || error == EPERM)
            fputs("Check read/write permissions on the APCI device nodes.\n", stderr);
        if (error == EBUSY) fputs("The APCI device is already in use.\n", stderr);
        goto cleanup;
    }
    if (failed("BRD_GetCapabilities", BRD_GetCapabilities(board, &caps))) goto cleanup;
    if (!caps.ADCCount || !caps.DMAAvailable) { fputs("ADC DMA is unavailable.\n", stderr); goto cleanup; }
    if (caps.StructSize >= sizeof(caps) && caps.MaxADCRateHz > 0 && ADC_RATE_HZ > caps.MaxADCRateHz) {
        fprintf(stderr, "%s: requested ADC rate %.9g conversions/s/ADC exceeds the maximum of "
                "%.9g conversions/s/ADC (%.9g ksps).\n", caps.Name,
                ADC_RATE_HZ, caps.MaxADCRateHz, caps.MaxADCRateHz / 1000.0);
        goto cleanup;
    }

    /* 2. Configure all channels for single-ended, bipolar 10 V sampling. */
    if (failed("ADC_SetDifferential", ADC_SetDifferential(board, 0))) goto cleanup;
    if (failed("ADC_SetChannelCount", ADC_SetChannelCount(board, ciAll))) goto cleanup;
    if (failed("ADC_SetRange1", ADC_SetRange1(board, ciAll, rcBipolar10))) goto cleanup;
    if (failed("ADC_SetRate", ADC_SetRate(board, ADC_RATE_HZ))) goto cleanup;
    if (failed("ADC_GetRate", ADC_GetRate(board, &actual_rate))) goto cleanup;
    if (failed("ADC_ConfigureAcquire", ADC_ConfigureAcquire(board, ctAuto, 0))) goto cleanup;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction"); goto cleanup;
    }
    printf("Capturing %u ADC results from %s at %.9g conversions/s/ADC.\n",
           CAPTURE_SAMPLES, caps.Name, actual_rate);

    /* 3. Start a finite capture, then wait for and consume DMA-backed data. */
    if (!now_seconds(&start)) goto cleanup;
    if (failed("ADC_AcquireBlock", ADC_AcquireBlock(board, CAPTURE_SAMPLES))) {
        fputs("Set AIOAIO_DEBUG=1 to trace hardware setup and register checks.\n", stderr);
        (void)ADC_GetAcquisitionStatus(board, &status);
        goto cleanup;
    }
    started = 1;
    for (;;) {
        TUInt32 got = 0;
        if (interrupted) { result = 128 + interrupted; fputs("Capture interrupted.\n", stderr); break; }
        if (!now_seconds(&now)) break;
        if (now - start >= TIMEOUT_SECONDS) { fputs("Capture deadline expired.\n", stderr); break; }
        error = ADC_GetDataRaw(board, BUFFER_SAMPLES, &got, buffer);
        if (!error) {
            if (!got || got > BUFFER_SAMPLES || got > CAPTURE_SAMPLES - delivered) {
                fputs("Unexpected sample count returned by the library.\n", stderr); break;
            }
            for (TUInt32 i = 0; i < got && shown < PREVIEW_SAMPLES; ++i) preview[shown++] = buffer[i];
            delivered += got; /* Process buffer[0..got-1] here. */
            continue;
        }
        if (error == ENODATA) { result = delivered == CAPTURE_SAMPLES ? 0 : 1; break; }
        if (error != EAGAIN) { failed("ADC_GetDataRaw", error); break; }
        error = ADC_WaitData(board, 100); /* Bounded wait keeps Ctrl-C responsive. */
        if (error && error != ETIMEDOUT && error != ENODATA) { failed("ADC_WaitData", error); break; }
    }

cleanup:
    /* 4. Stop, check the final counters, and always release the board. */
    if (started) {
        if (failed("ADC_AcquireEnd", ADC_AcquireEnd(board))) result = 1;
        error = ADC_GetAcquisitionStatus(board, &status);
        if (failed("ADC_GetAcquisitionStatus", error)) result = 1;
        else {
            if (status.ErrorCode || status.State != AIO_STATE_COMPLETE || status.DiscardedDMASlots ||
                status.CapturedSamples != CAPTURE_SAMPLES || status.DeliveredSamples != delivered ||
                delivered != CAPTURE_SAMPLES) { if (!result) result = 1; }
            printf("Captured %" PRIu32 " / %u ADC results; discarded DMA slots: %" PRIu64 ".\n",
                   delivered, CAPTURE_SAMPLES, status.DiscardedDMASlots);
        }
    }
    error = AIO_Shutdown();
    if (error) {
        failed(error == status.ErrorCode ? "AIO_Shutdown (earlier acquisition failure)" : "AIO_Shutdown", error);
        if (!result) result = 1;
    }
    if (!result) {
        printf("First %" PRIu32 " raw ADC words:", shown);
        for (TUInt32 i = 0; i < shown; ++i) printf(" 0x%08" PRIx32, preview[i]);
        putchar('\n');
    }
    return result;
}
