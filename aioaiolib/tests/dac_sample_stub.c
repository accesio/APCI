/* Public-API stubs for the example executables: no driver or device access. */
#include "AIOAIO.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static int opened, started, stopped, status_calls;
static TUInt32 channels, mode;
static int scenario(const char *name)
{
    const char *value = getenv("DAC_SAMPLE_CASE");
    return value && !strcmp(value, name);
}

const char *AIO_ErrorString(TWinErrCode error) { return strerror((int)error); }

TWinErrCode BRD_Open(const char *path, TBoardIndex *board)
{
    const char *expected = getenv("DAC_SAMPLE_DEVICE");
    assert(!scenario("no_open"));
    assert(!strcmp(path, expected ? expected : "/dev/apci/mpcie_aio16_16fds_0"));
    if (scenario("open_failure")) return ENOENT;
    opened = 1;
    *board = 7;
    return 0;
}

TWinErrCode DAC_GetWaveformCapabilities(TBoardIndex board, TDACWaveformCapabilities *caps)
{
    assert(opened && board == 7 && caps->StructSize == sizeof(*caps));
    if (scenario("unsupported")) return ENOTSUP;
    caps->ChannelCount = 4;
    caps->FIFOSamples = 8192;
    caps->FirmwareRevision = 0x12345678;
    caps->MaxSampleRateHz = 1000000;
    return 0;
}

TWinErrCode DAC_StartWaveform(TBoardIndex board, TUInt32 count, TUInt32 samples,
                             const TUInt16 *data, TDouble rate, TUInt32 refill)
{
    assert(opened && board == 7 && samples == 262144 && data);
    assert(rate * count == 1000000.0);
    channels = count;
    mode = refill;
    if (refill == AIO_DAC_REFILL_POLL) {
        assert(count == 2);
        assert(data[0] == 0 && data[1] == 0 && data[2] == 65535 && data[3] == 0);
        assert(data[4] == 0 && data[5] == 1 && data[6] == 65535 && data[7] == 1);
        assert(data[samples - 1] == 65535);
    } else {
        assert(refill == AIO_DAC_REFILL_IRQ && count == 1);
        assert(data[0] == 65535 && data[samples / 2] == 1);
        assert(data[samples / 4] >= 32767 && data[samples / 4] <= 32768);
    }
    if (scenario("start_failure")) return EIO;
    started = 1;
    return 0;
}

TWinErrCode DAC_GetWaveformStatus(TBoardIndex board, TDACWaveformStatus *status)
{
    assert(opened && started && board == 7 && status->StructSize == sizeof(*status));
    ++status_calls;
    status->State = stopped ? AIO_STATE_CANCELLED : AIO_STATE_RUNNING;
    status->ErrorCode = scenario("underrun") && status_calls > 1 ? EPIPE : 0;
    if (status->ErrorCode) status->State = AIO_STATE_FAILED;
    status->ChannelCount = channels;
    status->RefillMode = mode;
    status->UploadedSamples = 12345;
    status->Refills = 3;
    status->IRQWakeups = mode == AIO_DAC_REFILL_IRQ ? 2 : 0;
    status->ActualPointRateHz = 1000000.0 / channels;
    if (scenario("interrupt") && status_calls == 2) raise(SIGINT);
    return 0;
}

TWinErrCode DAC_StopWaveform(TBoardIndex board)
{
    assert(opened && started && !stopped && board == 7);
    stopped = 1;
    return scenario("underrun") ? EPIPE : 0;
}

TWinErrCode BRD_Close(TBoardIndex board)
{
    assert(opened && board == 7 && (!started || stopped));
    opened = 0;
    return 0;
}
