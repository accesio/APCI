#define _POSIX_C_SOURCE 200809L

/* Capture and benchmark utility. All board access goes through AIOAIO.h. */
#include "AIOAIO.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { READ_CAPACITY = 16384 };

struct options {
    const char *device;
    const char *csv;
    TBoardIndex board;
    TUInt32 samples;
    TChannelIndex channels;
    double seconds;
    double rate;
    double timeout;
    int benchmark;
    int differential;
    int list;
    int debug;
};

static volatile sig_atomic_t interrupted_signal;

static void on_signal(int signal_number)
{
    interrupted_signal = signal_number;
}

static void usage(FILE *out, const char *program)
{
    fprintf(out,
        "Usage: %s [--list] [--device PATH | --board INDEX] [options]\n"
        "  --list             List supported, accessible boards and exit\n"
        "  --device PATH      Open one exact device path (avoids discovery)\n"
        "  --board INDEX      Choose a discovered board (default: 0)\n"
        "  --samples N        Capture N scalar ADC results (default: 100000)\n"
        "  --seconds SEC      Derive count from actual rate and ADC count\n"
        "  --rate HZ          Conversions/sec per physical ADC (default: 10000)\n"
        "  --channels N       Total selected channels, split equally among ADCs\n"
        "                     (default: all channels for the selected mode)\n"
        "  --differential     Use differential inputs (default: single-ended)\n"
        "  --timeout SEC      Overall capture deadline (default: 30 seconds)\n"
        "  --csv FILE         Write indexed raw ADC words (default: samples.csv)\n"
        "  --benchmark        No CSV or per-batch output; measure host delivery\n"
        "  --debug            Trace hardware setup and register checks to stderr\n"
        "  --help             Show this help without accessing hardware\n"
        "\n--samples and --seconds are exclusive; --benchmark excludes --csv.\n"
        "An existing CSV file is overwritten. Capture configures the board.\n",
        program);
}

static int parse_integer(const char *text, uint64_t maximum, uint64_t *value)
{
    char *end;
    unsigned long long parsed;
    if (text[0] < '0' || text[0] > '9') return 0;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || *end || parsed > maximum) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_positive(const char *text, double *value)
{
    char *end;
    double parsed;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno || end == text || *end || !isfinite(parsed) || parsed <= 0) return 0;
    *value = parsed;
    return 1;
}

static int parse_options(int argc, char **argv, struct options *options)
{
    static const struct option long_options[] = {
        {"list", no_argument, NULL, 'l'},
        {"device", required_argument, NULL, 'd'},
        {"board", required_argument, NULL, 'b'},
        {"samples", required_argument, NULL, 'n'},
        {"seconds", required_argument, NULL, 's'},
        {"rate", required_argument, NULL, 'r'},
        {"channels", required_argument, NULL, 'c'},
        {"differential", no_argument, NULL, 'D'},
        {"timeout", required_argument, NULL, 't'},
        {"csv", required_argument, NULL, 'o'},
        {"benchmark", no_argument, NULL, 'B'},
        {"debug", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int selected_board = 0, selected_samples = 0, selected_csv = 0, choice;
    uint64_t number;
    *options = (struct options){.csv = "samples.csv", .samples = 100000,
        .channels = ciAll, .rate = 10000, .timeout = 30};
    while ((choice = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (choice) {
        case 'l': options->list = 1; break;
        case 'd': options->device = optarg; break;
        case 'b':
            if (!parse_integer(optarg, UINT64_MAX - 1, &number)) goto invalid;
            options->board = number;
            selected_board = 1;
            break;
        case 'n':
            if (!parse_integer(optarg, UINT32_MAX, &number) || !number) goto invalid;
            options->samples = (TUInt32)number;
            selected_samples = 1;
            break;
        case 's': if (!parse_positive(optarg, &options->seconds)) goto invalid; break;
        case 'r': if (!parse_positive(optarg, &options->rate)) goto invalid; break;
        case 't': if (!parse_positive(optarg, &options->timeout)) goto invalid; break;
        case 'c':
            if (!parse_integer(optarg, UINT32_MAX - 1, &number) || !number) goto invalid;
            options->channels = (TChannelIndex)number;
            break;
        case 'D': options->differential = 1; break;
        case 'o': options->csv = optarg; selected_csv = 1; break;
        case 'B': options->benchmark = 1; break;
        case 'v': options->debug = 1; break;
        case 'h': usage(stdout, argv[0]); return 1;
        default: return -1;
        }
    }
    if (optind != argc || (selected_board && options->device) ||
        (selected_samples && options->seconds > 0) ||
        (selected_csv && options->benchmark) ||
        (options->list && (options->device || selected_board)) ||
        (options->device && !options->device[0]) || !options->csv[0]) {
        fprintf(stderr, "Conflicting options, an empty path, or an unexpected argument.\n");
        return -1;
    }
    return 0;
invalid:
    fprintf(stderr, "Invalid value: %s\n", optarg);
    return -1;
}

static int report_api_error(const char *operation, TWinErrCode error)
{
    fprintf(stderr, "%s: %s (%" PRIu32 ")\n", operation, AIO_ErrorString(error), error);
    return 1;
}

static void report_discovery_error(const char *device, TWinErrCode error)
{
    report_api_error(device ? "BRD_Open" : "AIO_Initialize", error);
    if (device) fprintf(stderr, "APCI device: %s\n", device);
    switch (error) {
    case ENOENT:
        fputs(device ? "The requested device path was not found. Check the --device path.\n"
                     : "Could not find /dev/apci or one of its device nodes.\n", stderr);
        fputs("The APCI driver (apci.ko) may not be loaded, or no APCI cards may have been detected.\n"
              "Check that the driver is loaded and that device nodes exist under /dev/apci.\n", stderr);
        break;
    case ENODEV:
    case ENOTSUP:
        fputs(device ? "This device is not a supported AxIO ADC card.\n"
                     : "No supported AxIO ADC cards were found under /dev/apci.\n", stderr);
        fputs("Check that your card is detected by the APCI driver and supported by this version of libaioaio.\n", stderr);
        break;
    case EACCES:
    case EPERM:
        fputs("Access to APCI devices was denied. Check directory access and read/write permissions on the device nodes.\n", stderr);
        break;
    case EBUSY:
        fputs("An APCI device is already in use. Stop the other acquisition or select another device with --device PATH.\n", stderr);
        break;
    default:
        break;
    }
}

static int clock_seconds(clockid_t clock_id, double *value)
{
    struct timespec time;
    if (clock_gettime(clock_id, &time)) {
        perror("clock_gettime");
        return 0;
    }
    *value = (double)time.tv_sec + (double)time.tv_nsec / 1000000000.0;
    return 1;
}

static int list_boards(void)
{
    TBoardIndex highest = BRD_GetHighestBoardIndex();
    if (highest == biNone) {
        puts("No supported, accessible boards found.");
        return 0;
    }
    for (TBoardIndex board = 0; board <= highest; ++board) {
        TBoardCapabilities caps = {.StructSize = sizeof(caps)};
        TWinErrCode error = BRD_GetCapabilities(board, &caps);
        if (error) return report_api_error("BRD_GetCapabilities", error);
        printf("%" PRIu64 ": %s\n  %s; device 0x%04" PRIx32
               ", %" PRIu32 " ADC(s), %" PRIu32 " input channels, %" PRIu32
               " bits, DMA %s\n", board, caps.Name, caps.DevicePath,
               caps.DeviceID, caps.ADCCount, caps.MaxChannels, caps.ResolutionBits,
               caps.DMAAvailable ? "available" : "unavailable");
        if (caps.StructSize >= sizeof(caps) && caps.MaxADCRateHz > 0)
            printf("  Maximum ADC rate: %.9g conversions/s/ADC (%.9g ksps)\n",
                   caps.MaxADCRateHz, caps.MaxADCRateHz / 1000.0);
    }
    return 0;
}

static const char *state_name(TUInt32 state)
{
    switch (state) {
    case AIO_STATE_IDLE: return "idle";
    case AIO_STATE_RUNNING: return "running";
    case AIO_STATE_COMPLETE: return "complete";
    case AIO_STATE_CANCELLED: return "cancelled";
    case AIO_STATE_FAILED: return "failed";
    default: return "unknown";
    }
}

static int capture(TBoardIndex board, const struct options *options,
                   const TBoardCapabilities *caps)
{
    TUInt32 target = options->samples, *data = NULL;
    TUInt64 delivered = 0;
    TADCAcquisitionStatus status = {.StructSize = sizeof(status)};
    TWinErrCode error;
    FILE *csv = NULL;
    double rate, start = 0, finish = 0, cpu_start = 0, cpu_finish = 0;
    double now, first_data = -1;
    int result = 1, started = 0, status_valid = 0, csv_created = 0;
    struct sigaction action = {0};

    if (!caps->ADCCount || !caps->DMAAvailable) {
        fputs("This board does not provide supported ADC DMA acquisition.\n", stderr);
        return 1;
    }
    if (caps->StructSize >= sizeof(*caps) && caps->MaxADCRateHz > 0 &&
        options->rate > caps->MaxADCRateHz) {
        fprintf(stderr, "%s: requested ADC rate %.9g conversions/s/ADC exceeds the maximum of "
                "%.9g conversions/s/ADC (%.9g ksps).\n",
                caps->Name, options->rate, caps->MaxADCRateHz, caps->MaxADCRateHz / 1000.0);
        return 1;
    }
    error = ADC_SetDifferential(board, (TLongBool)options->differential);
    if (error) return report_api_error("ADC_SetDifferential", error);
    error = ADC_SetChannelCount(board, options->channels);
    if (error) return report_api_error("ADC_SetChannelCount", error);
    error = ADC_SetRange1(board, ciAll, rcBipolar10);
    if (error) return report_api_error("ADC_SetRange1", error);
    error = ADC_SetRate(board, options->rate);
    if (error) return report_api_error("ADC_SetRate", error);
    error = ADC_GetRate(board, &rate);
    if (error) return report_api_error("ADC_GetRate", error);
    if (!isfinite(rate) || rate <= 0) {
        fputs("The board returned an invalid acquisition rate.\n", stderr);
        return 1;
    }
    if (options->seconds > 0) {
        long double count = (long double)options->seconds * rate * caps->ADCCount;
        if (!isfinite(count) || count <= 0 || count > UINT32_MAX) {
            fputs("Duration and actual rate require more than 1..UINT32_MAX results.\n", stderr);
            return 1;
        }
        target = (TUInt32)count;
        if ((long double)target < count) ++target;
    }
    error = ADC_ConfigureAcquire(board, ctAuto, 0);
    if (error) return report_api_error("ADC_ConfigureAcquire", error);

    data = malloc(sizeof(*data) * READ_CAPACITY);
    if (!data) { perror("allocate read buffer"); goto cleanup; }
    if (!options->benchmark) {
        csv = fopen(options->csv, "w");
        if (!csv) { perror(options->csv); goto cleanup; }
        csv_created = 1;
        (void)setvbuf(csv, NULL, _IOFBF, 1024 * 1024);
        if (fprintf(csv, "sample_index,raw_word\n") < 0) {
            perror("CSV header");
            goto cleanup;
        }
    }
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        goto cleanup;
    }
    printf("%s: %s (%s)\n", options->benchmark ? "Benchmark" : "Capture",
           caps->Name, caps->DevicePath);
    printf("Requested results: %" PRIu32 "; actual configured rate: %.9g conversions/s/ADC\n",
           target, rate);
    printf("Expected scalar result rate: %.9g results/s across %" PRIu32 " ADC(s)\n",
           rate * caps->ADCCount, caps->ADCCount);
    fflush(stdout);

    if (!clock_seconds(CLOCK_PROCESS_CPUTIME_ID, &cpu_start) ||
        !clock_seconds(CLOCK_MONOTONIC, &start)) goto cleanup;
    error = ADC_AcquireBlock(board, target);
    if (error) {
        const char *debug = getenv("AIOAIO_DEBUG");
        report_api_error("ADC_AcquireBlock", error);
        if (!options->debug && (!debug || strcmp(debug, "1")))
            fputs("Rerun with --debug to identify the failing hardware setup operation and register readbacks.\n", stderr);
        goto cleanup;
    }
    started = 1;

    for (;;) {
        TUInt32 got = 0;
        if (interrupted_signal) {
            fprintf(stderr, "Capture interrupted by signal %d.\n", (int)interrupted_signal);
            result = 128 + interrupted_signal;
            break;
        }
        if (!clock_seconds(CLOCK_MONOTONIC, &now)) break;
        if (now - start >= options->timeout) {
            fputs("Capture deadline expired.\n", stderr);
            break;
        }
        error = ADC_GetDataRaw(board, READ_CAPACITY, &got, data);
        if (!error) {
            if (!got || got > READ_CAPACITY || delivered + got > target) {
                fputs("Library returned an unexpected result count.\n", stderr);
                break;
            }
            if (first_data < 0) {
                if (!clock_seconds(CLOCK_MONOTONIC, &now)) break;
                first_data = now - start;
            }
            TUInt64 first_index = delivered;
            delivered += got;
            if (csv) {
                for (TUInt32 i = 0; i < got; ++i) {
                    if (fprintf(csv, "%" PRIu64 ",0x%08" PRIx32 "\n",
                                first_index + i, data[i]) < 0) {
                        perror("CSV data");
                        goto cleanup;
                    }
                }
            }
            continue;
        }
        if (error == ENODATA) {
            if (delivered == target) result = 0;
            else fprintf(stderr, "Capture ended after %" PRIu64 " of %" PRIu32 " results.\n",
                         delivered, target);
            break;
        }
        if (error != EAGAIN) {
            report_api_error("ADC_GetDataRaw", error);
            break;
        }
        if (!clock_seconds(CLOCK_MONOTONIC, &now)) break;
        double remaining_ms = (options->timeout - (now - start)) * 1000.0;
        if (remaining_ms <= 0) continue;
        TUInt32 wait_ms = remaining_ms >= 100 ? 100 : (TUInt32)remaining_ms;
        if (!wait_ms) wait_ms = 1;
        error = ADC_WaitData(board, wait_ms);
        if (error && error != ETIMEDOUT && error != ENODATA && error != ECANCELED) {
            report_api_error("ADC_WaitData", error);
            break;
        }
    }

cleanup:
    if (started) {
        if (!clock_seconds(CLOCK_MONOTONIC, &finish) ||
            !clock_seconds(CLOCK_PROCESS_CPUTIME_ID, &cpu_finish)) result = 1;
        error = ADC_AcquireEnd(board);
        if (error) { report_api_error("ADC_AcquireEnd", error); result = 1; }
        error = ADC_GetAcquisitionStatus(board, &status);
        if (error) { report_api_error("ADC_GetAcquisitionStatus", error); result = 1; }
        else {
            status_valid = 1;
            if (status.ErrorCode || status.DiscardedDMASlots ||
                status.State != AIO_STATE_COMPLETE || status.CapturedSamples != target ||
                status.DeliveredSamples != delivered || delivered != target) {
                if (result == 0) result = 1;
            }
        }
    }
    if (csv && fclose(csv)) { perror("close CSV"); result = 1; }
    free(data);

    if (started) {
        double wall = finish - start, cpu = cpu_finish - cpu_start;
        printf("Result: %s; delivered %" PRIu64 " / %" PRIu32 " scalar ADC results\n",
               result == 0 ? "success" : "incomplete or failed", delivered, target);
        if (status_valid) {
            printf("Library state: %s; captured %" PRIu64 "; queued %" PRIu64
                   "; selected channels %" PRIu32 "\n", state_name(status.State),
                   status.CapturedSamples, status.QueuedSamples, status.ChannelCount);
            printf("DMA completions: %" PRIu64 "; DMA bytes: %" PRIu64
                   "; discarded DMA slots: %" PRIu64 "\n",
                   status.DMACompletions, status.DMABytes, status.DiscardedDMASlots);
            if (status.ErrorCode) report_api_error("Latched acquisition error", status.ErrorCode);
        }
        if (wall > 0 && cpu >= 0) {
            printf("Host capture-loop wall time: %.6f s; process CPU: %.6f s (%.1f%% of one CPU)\n",
                   wall, cpu, cpu * 100.0 / wall);
            printf("Host delivery throughput: %.3f results/s, %.3f MiB/s of raw words\n",
                   delivered / wall, (double)delivered * sizeof(TUInt32) / wall / 1048576.0);
        }
        if (first_data >= 0) printf("First data delivered to this process: %.6f s after start\n", first_data);
        else puts("First-data latency: unavailable (no data delivered)");
        puts("Host timings measure API delivery, not ADC conversion timing or sampling jitter.");
        if (csv_created) printf("CSV: %s%s\n", options->csv, result ? " (partial or failed capture)" : "");
    }
    return result;
}

int main(int argc, char **argv)
{
    struct options options;
    TBoardCapabilities caps = {.StructSize = sizeof(caps)};
    TBoardIndex board;
    TWinErrCode error, acquisition_error = 0;
    int result = parse_options(argc, argv, &options);
    if (result > 0) return 0;
    if (result < 0) { usage(stderr, argv[0]); return 2; }
    if (options.debug && setenv("AIOAIO_DEBUG", "1", 1)) {
        perror("enable AIOAIO_DEBUG");
        return 1;
    }
    board = options.board;
    error = options.device ? BRD_Open(options.device, &board) : AIO_Initialize();
    if (error) {
        report_discovery_error(options.device, error);
        (void)AIO_Shutdown();
        return 1;
    }
    if (options.list) result = list_boards();
    else {
        error = BRD_GetCapabilities(board, &caps);
        if (error) result = report_api_error("BRD_GetCapabilities", error);
        else {
            TADCAcquisitionStatus status = {.StructSize = sizeof(status)};
            result = capture(board, &options, &caps);
            error = ADC_GetAcquisitionStatus(board, &status);
            if (error) {
                report_api_error("ADC_GetAcquisitionStatus before shutdown", error);
                if (!result) result = 1;
            } else if (status.State == AIO_STATE_FAILED) {
                acquisition_error = status.ErrorCode;
            }
        }
    }
    error = AIO_Shutdown();
    if (error) {
        report_api_error(error == acquisition_error
            ? "AIO_Shutdown (reporting the earlier acquisition failure)" : "AIO_Shutdown", error);
        if (!result) result = 1;
    }
    return result;
}
