#ifndef AIOAIO_LINUX_H_INCLUDED
#define AIOAIO_LINUX_H_INCLUDED

/*
 * Linux AxIO API, ABI version 1.
 * Return values are zero on success or a positive Linux errno value.
 * This interface is not binary-compatible with the existing Windows DLL.
 *
 * ADC configuration, acquisition, callbacks, and queued data belong to a board.
 * Hardware channel and scan geometry are determined by the board model.
 * Channel identifiers address board channels; dual-ADC boards retain their
 * simultaneous-pair ordering (0, 8, 1, 9, ...) in raw acquisition data.
 * Prototype devices are outside the supported hardware scope.
 *
 * Derived from the AIOAIO public header. TWinErrCode is retained as a type
 * name only; callers must use Linux errno constants, not Windows errors.
 * Unsupported milestone operations return ENOTSUP. See README.md.
 */

#include <stdint.h>
#include <stddef.h>
#include <errno.h>

typedef uint8_t TUInt8, *TPUInt8;
typedef uint16_t TUInt16, *TPUInt16;
typedef uint32_t TUInt32, *TPUInt32;
typedef uint64_t TUInt64, *TPUInt64;
// Matches Delphi Cardinal: this API type is always 32-bit.
typedef uint32_t TUNative, *TPUNative;
typedef float TSingle, *TPSingle;
typedef double TDouble, *TPDouble;
typedef char TAnsiChar, *TPAnsiChar;
// Wide strings use 16-bit UTF-16 code units.
typedef uint16_t TWideChar, *TPWideChar;
typedef void *TPointer;

typedef TUInt32 TWinErrCode, *TPWinErrCode;
typedef TUInt64 TSerialNumber, *TPSerialNumber;
typedef TUInt32 TLongBool;

typedef TUInt64 TBoardIndex;
#define biAuto ((TBoardIndex)(0))
#define biNone ((TBoardIndex)(UINT64_MAX))

typedef TUInt32 TChannelIndex;
#define ciAll ((TChannelIndex)(UINT32_MAX))

typedef TUInt32 TRangeCode, *TPRangeCode;
#define rcNoChange ((TRangeCode)(UINT32_MAX))
#define rcUnipolar10 ((TRangeCode)(0x30313055))
#define rcUnipolar5 ((TRangeCode)(0x35303055))
#define rcBipolar24 ((TRangeCode)(0x3432E142))
#define rcBipolar20 ((TRangeCode)(0x3032E142))
#define rcBipolar12 ((TRangeCode)(0x3231E142))
#define rcBipolar10 ((TRangeCode)(0x3031E142))
#define rcBipolar5 ((TRangeCode)(0x3530E142))
#define rcBipolar2_5 ((TRangeCode)(0xBD32E142))
#define rcBipolar1_25 ((TRangeCode)(0xBC31E142))
#define rcBipolar_625 ((TRangeCode)(0x3835E142))
#define rcBipolar_3125 ((TRangeCode)(0x3331E142))
/* AD5755 current ranges; DAC_OutputV takes milliamps for these ranges. */
#define rcCurrent420 ((TRangeCode)(0x30323449))
#define rcCurrent020 ((TRangeCode)(0x30323049))
#define rcCurrent024 ((TRangeCode)(0x34323049))
#define rcRangeCode0 ((TRangeCode)(0x0))
#define rcRangeCode1 ((TRangeCode)(0x1))
#define rcRangeCode2 ((TRangeCode)(0x2))
#define rcRangeCode3 ((TRangeCode)(0x3))
#define rcRangeCode4 ((TRangeCode)(0x4))
#define rcRangeCode5 ((TRangeCode)(0x5))
#define rcRangeCode6 ((TRangeCode)(0x6))
#define rcRangeCode7 ((TRangeCode)(0x7))

typedef TUInt32 TIRQIndex;
#define iiOnly ((TIRQIndex)(0))

typedef TUInt32 TChunkingType;
#define ctAuto ((TChunkingType)(0))
#define ctSize ((TChunkingType)(1))
#define ctRate ((TChunkingType)(2))

typedef TUInt32 TCallbackType;
#define ctRaw ((TCallbackType)(0x776152))
#define ctCounts ((TCallbackType)(0x6E756F43))
#define ctV ((TCallbackType)(0x746C6F56))
#define ctNone ((TCallbackType)(0xFFFFFFFF))

/* StructSize must be initialized to sizeof(the struct) by the caller.
 * BRD_GetCapabilities returns the size of the prefix it filled. Existing
 * callers with the original structure receive only that original prefix. */
typedef struct {
    TUInt32 StructSize;
    TUInt32 DeviceID;
    TUInt32 ADCCount;
    TUInt32 ChannelsPerADC;
    TUInt32 MaxChannels;
    TUInt32 ResolutionBits;
    TUInt32 FIFORowBytes;
    TUInt32 DMAAvailable;
    char DevicePath[256];
    char Name[96];
    /* Per physical ADC, shared by its selected channels. Appended field:
     * check returned StructSize before using it with an older library. */
    TDouble MaxADCRateHz;
    /* Immediate DAC capability, separate from timed waveform capability.
     * Zero on ADC-only cards. Check StructSize before reading appended fields. */
    TUInt32 DACCount;
    TUInt32 DACResolutionBits;
} TBoardCapabilities;

#define AIO_STATE_IDLE 0u
#define AIO_STATE_RUNNING 1u
#define AIO_STATE_COMPLETE 2u
#define AIO_STATE_CANCELLED 3u
#define AIO_STATE_FAILED 4u

typedef struct {
    TUInt32 StructSize;
    TUInt32 State;
    TUInt32 ErrorCode;
    TUInt32 ChannelCount;
    TUInt64 RequestedSamples;
    TUInt64 CapturedSamples;
    TUInt64 DeliveredSamples;
    TUInt64 QueuedSamples;
    TUInt64 DiscardedDMASlots;
    TUInt64 DMACompletions;
    TUInt64 DMABytes;
    TUInt64 ElapsedNanoseconds;
    TUInt64 FirstDataNanoseconds;
    TDouble ActualRateHz;
} TADCAcquisitionStatus;

/* Timed, repeating DAC output. Rates count points (one result per selected
 * channel); the aggregate sample rate is PointRateHz * ChannelCount. */
#define AIO_DAC_REFILL_POLL 0u
#define AIO_DAC_REFILL_IRQ 1u
typedef struct {
    TUInt32 StructSize;
    TUInt32 ChannelCount;
    TUInt32 FIFOSamples;
    TUInt32 FirmwareRevision;
    TDouble MaxSampleRateHz;
} TDACWaveformCapabilities;
typedef struct {
    TUInt32 StructSize;
    TUInt32 State;
    TUInt32 ErrorCode;
    TUInt32 ChannelCount;
    TUInt32 RefillMode;
    TUInt32 FIFOSamples;
    TUInt64 UploadedSamples;
    TUInt64 Refills;
    TUInt64 IRQWakeups;
    TUInt64 ElapsedNanoseconds;
    TDouble ActualPointRateHz;
} TDACWaveformStatus;

#ifdef __cplusplus
extern "C" {
#define AIOAIO_DEFAULT(value) = value
#else
#define AIOAIO_DEFAULT(value)
#endif

/* Callback signatures selected by ctRaw, ctCounts, and ctV respectively. */
typedef void (*TADCCallbackRaw)(TBoardIndex iBoard, TUInt32 DataCt,
                               TPUInt32 pDataRaw);
typedef void (*TADCCallbackCounts)(TBoardIndex iBoard, TUInt32 DataCt,
                                  TPUInt16 pData, TPUInt16 pStatus,
                                  TPUInt16 pTimestamp);
typedef void (*TADCCallbackV)(TBoardIndex iBoard, TUInt32 DataCt,
                             TPDouble pData, TPUInt16 pStatus,
                             TPUInt16 pTimestamp);

/* Discovery is explicit; merely loading this library never accesses hardware.
 * Initialize discovers /dev/apci. Open selects one exact device path.
 * Shutdown/Close stop acquisition and release owned resources.
 */
TWinErrCode AIO_Initialize(void);
TWinErrCode AIO_Shutdown(void);
const char *AIO_ErrorString(TWinErrCode error);
TWinErrCode BRD_Open(const char *path, TBoardIndex *pBoard);
TWinErrCode BRD_Close(TBoardIndex iBoard);
TWinErrCode BRD_GetCapabilities(TBoardIndex iBoard, TBoardCapabilities *pCaps);
TWinErrCode ADC_GetAcquisitionStatus(TBoardIndex iBoard, TADCAcquisitionStatus *pStatus);
/* Wait for queued data or a terminal state. ETIMEDOUT means the deadline
 * elapsed; ENODATA means completed/drained; ECANCELED means cancelled/drained.
 * GetData functions are nonblocking and return EAGAIN while running/empty.
 */
TWinErrCode ADC_WaitData(TBoardIndex iBoard, TUInt32 TimeoutMs);
TBoardIndex BRD_GetHighestBoardIndex(void);
TWinErrCode BRD_GetNameA(TBoardIndex iBoard, TUInt32 BufLen, TPAnsiChar pNameBuf AIOAIO_DEFAULT(NULL), TPUInt32 pNameLen AIOAIO_DEFAULT(NULL));
TWinErrCode BRD_GetNameW(TBoardIndex iBoard, TUInt32 BufLen, TPWideChar pNameBuf AIOAIO_DEFAULT(NULL), TPUInt32 pNameLen AIOAIO_DEFAULT(NULL));
/* Deprecated: no serial-number implementation is planned. Returns ENOTSUP
 * and writes zero when pSN is nonnull; retained only for ABI compatibility. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((deprecated("Serial-number retrieval is unsupported; returns ENOTSUP")))
#endif
TWinErrCode BRD_GetSerialNumber(TBoardIndex iBoard, TPSerialNumber pSN);
TWinErrCode BRD_GetDeviceID(TBoardIndex iBoard, TPUInt32 pDeviceID);
/* Stop ADC/DAC workers and reset the board, clearing queued data and latched
 * acquisition errors after a successful reset. Restores software ADC defaults
 * and automatic chunk sizing; keeps callback registration. Hardware reset can
 * change DAC and DIO outputs. Set DAC ranges again before DAC_OutputV. */
TWinErrCode BRD_Reset(TBoardIndex iBoard);

/* Immediate DAC output: ciAll addresses every output. SetRangeAll repeats
 * its last supplied range to remaining outputs; zero count is a no-op and
 * excess ranges are ignored. rcNoChange preserves that channel's range.
 * Configure a range before DAC_OutputV; unknown ranges return ENODATA.
 * Values are volts, or milliamps for AD5755 current ranges. Counts are 0..65535;
 * OutputV rounds to nearest (ties to even) using 65536 counts per span. The
 * physical lower endpoint is included; positive full scale and values rounding
 * beyond 65535 return ERANGE. NaN/infinity and invalid ranges return EINVAL.
 * Broadcast values/ranges are validated before the first output write. A
 * hardware failure can leave a partially applied request; a failed range write
 * makes the affected range unknown until successfully reconfigured.
 * Active ADC acquisition or DAC waveform playback returns EBUSY. ADC-only
 * boards return ENOTSUP. Immediate outputs persist across Close/Shutdown.
 */
TWinErrCode DAC_SetRange1(TBoardIndex iBoard, TChannelIndex Ch AIOAIO_DEFAULT(ciAll), TRangeCode Range AIOAIO_DEFAULT(rcBipolar10));
TWinErrCode DAC_SetRangeAll(TBoardIndex iBoard, TUInt32 RangeCt, TPRangeCode pRange);
TWinErrCode DAC_OutputV(TBoardIndex iBoard, TChannelIndex Ch, TDouble V);
TWinErrCode DAC_OutputCounts(TBoardIndex iBoard, TChannelIndex Ch, TUInt64 Counts);

/* FDS waveform support is queried separately from ADC capabilities. No DAC
 * output is changed by the capability query. Unsupported boards return ENOTSUP.
 * Start copies SampleCt interleaved 16-bit counts for channels 0..ChannelCount-1
 * and repeats that waveform until Stop/Close/Shutdown. SampleCt must contain
 * complete points. Output range is bipolar 10 V: 0=negative full scale,
 * 32768=0 V, 65535=positive full scale minus one LSB. Start/Stop set all DAC
 * outputs to zero volts. Starting an ADC capture while DAC playback is active,
 * or vice versa, returns EBUSY (one APCI IRQ consumer per board).
 * Status counters describe host FIFO uploads, not measured physical playback.
 * In IRQ mode, status queries request an asynchronous FIFO health check so
 * missed interrupts cannot indefinitely hide an underrun from a monitor.
 * An underrun stops playback and reports EPIPE. Stop joins the refill worker;
 * it also reports an earlier playback failure. No callbacks are invoked.
 */
TWinErrCode DAC_GetWaveformCapabilities(TBoardIndex iBoard, TDACWaveformCapabilities *pCaps);
TWinErrCode DAC_StartWaveform(TBoardIndex iBoard, TUInt32 ChannelCount, TUInt32 SampleCt,
                            const TUInt16 *pSamples, TDouble PointRateHz, TUInt32 RefillMode);
TWinErrCode DAC_GetWaveformStatus(TBoardIndex iBoard, TDACWaveformStatus *pStatus);
TWinErrCode DAC_StopWaveform(TBoardIndex iBoard);

TWinErrCode ADC_SetRange1(TBoardIndex iBoard, TChannelIndex Ch AIOAIO_DEFAULT(ciAll), TRangeCode RangeCode AIOAIO_DEFAULT(rcBipolar10));
TWinErrCode ADC_SetRangeAll(TBoardIndex iBoard, TUInt32 RangeCt, TPRangeCode pRangeCodes);
/* Hz is conversions/second per physical ADC, shared among its selected
 * channels. Total scalar result rate is Hz * ADCCount. Both ADCs share timing.
 * Requests above BRD_GetCapabilities().MaxADCRateHz return ERANGE without
 * changing the configured rate. Divider rounding never exceeds that limit.
 * ChannelCount is the total selected input count, evenly split across ADCs;
 * differential mode halves the maximum count. Channels retain physical IDs.
 */
TWinErrCode ADC_SetRate(TBoardIndex iBoard, TDouble Hz);
TWinErrCode ADC_GetRate(TBoardIndex iBoard, TPDouble pHz);
TWinErrCode ADC_SetChannelCount(TBoardIndex iBoard, TChannelIndex ChCt AIOAIO_DEFAULT(ciAll));
TWinErrCode ADC_SetDifferential(TBoardIndex iBoard, TLongBool bDifferential AIOAIO_DEFAULT(true));
/* Immediate reads require idle ADC/DAC workers and do not consume queued DMA
 * data. Scan buffers need MaxChannels elements, indexed by board channel:
 * 0..7 on ADC0 and 8..15 on ADC1. In differential mode the active pair indices
 * are 0..3 and 8..11; inactive slots contain 0x80000000 (raw) or NaN (volts).
 * ADC_SetRange1/All retain physical-input range indexing: differential pair
 * indices 0..3 use range entries 0,2,4,6 (and 8,10,12,14 for ADC1).
 * Scans visit all inputs in the selected single-ended/differential mode,
 * regardless of ADC_SetChannelCount. Single reads return the selected slot.
 * Range overrides apply only to this call; rcNoChange uses configured ranges.
 * Streaming DMA continues to use native simultaneous-pair order.
 */
TWinErrCode ADC_GetImmediateScanV(TBoardIndex iBoard, TPDouble pVData, TRangeCode Range AIOAIO_DEFAULT(rcNoChange));
TWinErrCode ADC_GetImmediateScanRaw(TBoardIndex iBoard, TPUInt32 pRawData, TRangeCode Range AIOAIO_DEFAULT(rcNoChange));
TWinErrCode ADC_GetImmediateV(TBoardIndex iBoard, TPDouble V, TChannelIndex Ch, TRangeCode Range AIOAIO_DEFAULT(rcNoChange));
TWinErrCode ADC_GetImmediateRaw(TBoardIndex iBoard, TPUInt32 Data, TChannelIndex Ch, TRangeCode Range AIOAIO_DEFAULT(rcNoChange));

/* ctAuto uses 1024 FIFO rows; ctSize sets the raw 32-bit word count per DMA
 * slot, rounded up to a complete FIFO row. ctRate requests Chunk batches per
 * second: ceil(actual_rate * ADCCount / (Chunk * raw_words_per_row)), with
 * at least one row. A result exceeding FIFO capacity returns ERANGE. The
 * rate-based size is recalculated at start after any ADC_SetRate changes.
 * Finite captures can shorten a slot to avoid a long wait for a tiny request.
 * SampleCt/DataCt count valid ADC results across the board, not scans or rows.
 * A finite capture returns exactly SampleCt results; final DMA padding is ignored.
 */
TWinErrCode ADC_ConfigureAcquire(TBoardIndex iBoard, TChunkingType ChunkingType, TUInt32 Chunk);
TWinErrCode ADC_AcquireBlock(TBoardIndex iBoard, TUNative SampleCt);
TWinErrCode ADC_AcquireStream(TBoardIndex iBoard);
TWinErrCode ADC_AcquireEnd(TBoardIndex iBoard);
/* Register while idle, with no queued data: ctRaw/ctCounts/ctV require a
 * matching nonnull callback; ctNone with NULL unregisters it. Callback mode
 * replaces queued delivery: GetData/WaitData/PollData return EBUSY while a
 * callback is registered. Callbacks run on the acquisition worker with no
 * library locks held. Arrays are borrowed until the callback returns; copy
 * anything needed later. Counts/status/volts follow native DMA order and
 * timestamps are zero. Slow callbacks can cause a reported DMA overflow.
 * ADC_GetAcquisitionStatus and board name, device-ID and capability queries
 * are allowed from a callback. Calling
 * ADC_AcquireEnd on its own board requests a deferred stop and returns zero
 * (terminal state CANCELLED, including when called from the final batch);
 * other control/lifecycle calls return EDEADLK before taking locks. External
 * stop/close/shutdown waits for any in-flight callback to finish. A C++
 * exception escaping the callback ends acquisition with EIO.
 */
TWinErrCode ADC_SetCallback(TBoardIndex iBoard, TCallbackType CallbackType, TPointer pCallback AIOAIO_DEFAULT(NULL));
TWinErrCode ADC_GetDataV(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPDouble pData AIOAIO_DEFAULT(NULL), TPUInt16 pStatus AIOAIO_DEFAULT(NULL), TPUInt16 pTimestamp AIOAIO_DEFAULT(NULL));
TWinErrCode ADC_GetDataCounts(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPUInt16 pData AIOAIO_DEFAULT(NULL), TPUInt16 pStatus AIOAIO_DEFAULT(NULL), TPUInt16 pTimestamp AIOAIO_DEFAULT(NULL));
TWinErrCode ADC_GetDataRaw(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPUInt32 pDataRaw AIOAIO_DEFAULT(NULL));
TWinErrCode ADC_PollData(TBoardIndex iBoard);
TWinErrCode ADC_ClearData(TBoardIndex iBoard);

/* Raw register access uses byte offsets in logical BAR[1] (BAR_REGISTER),
 * within its 4 KiB native register aperture. Use the width required by the
 * hardware register map; native control registers generally require 32 bits.
 * The library must be idle. Offsets must be aligned to the transfer width.
 * Scalar reads return zero on failure and set errno; errno is zero on success.
 * Writes and block operations return a positive errno on failure.
 * INS/OUTS repeat at the SAME register offset (FIFO/port semantics), not at
 * successive addresses. DataCt counts elements, not bytes. A failed block can
 * have transferred a prefix; no count is returned, so do not blindly retry.
 * Raw writes invalidate cached DAC ranges; configure a range again before
 * DAC_OutputV. High-level ADC configuration is reapplied on the next capture.
 * Enabling ADC-trigger/DMA-done IRQs at +0x40 requires a DMA ring previously
 * prepared by this library; otherwise the raw write returns EBUSY.
 */
TUInt8 PIO_In8(TBoardIndex iBoard, TUInt32 Offset);
TUInt16 PIO_In16(TBoardIndex iBoard, TUInt32 Offset);
TUInt32 PIO_In32(TBoardIndex iBoard, TUInt32 Offset);
TWinErrCode PIO_Out8(TBoardIndex iBoard, TUInt32 Offset, TUInt8 Data);
TWinErrCode PIO_Out16(TBoardIndex iBoard, TUInt32 Offset, TUInt16 Data);
TWinErrCode PIO_Out32(TBoardIndex iBoard, TUInt32 Offset, TUInt32 Data);
TWinErrCode PIO_INS8(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt8 pData);
TWinErrCode PIO_INS16(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt16 pData);
TWinErrCode PIO_INS32(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt32 pData);
TWinErrCode PIO_OUTS8(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt8 pData);
TWinErrCode PIO_OUTS16(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt16 pData);
TWinErrCode PIO_OUTS32(TBoardIndex iBoard, TUInt32 Offset, TUInt32 DataCt, TPUInt32 pData);
/* Direct IRQ handling is not implemented; these three calls return ENOTSUP. */
TWinErrCode PIO_WaitForIRQ(TBoardIndex iBoard, TIRQIndex IRQIndex AIOAIO_DEFAULT(iiOnly));
TWinErrCode PIO_PollForIRQ(TBoardIndex iBoard, TIRQIndex IRQIndex AIOAIO_DEFAULT(iiOnly));
TWinErrCode PIO_AbortIRQWait(TBoardIndex iBoard, TIRQIndex IRQIndex AIOAIO_DEFAULT(iiOnly));

#undef AIOAIO_DEFAULT
#ifdef __cplusplus
} /* extern "C" */

/* Optional C++ overloads; the exported functions above also work from C. */
inline TWinErrCode BRD_GetName(TBoardIndex iBoard, TUInt32 BufLen, TPAnsiChar pNameBuf = NULL, TPUInt32 pNameLen = NULL)
{
	return BRD_GetNameA(iBoard, BufLen, pNameBuf, pNameLen);
}

inline TWinErrCode BRD_GetName(TBoardIndex iBoard, TUInt32 BufLen, TPWideChar pNameBuf = NULL, TPUInt32 pNameLen = NULL)
{
	return BRD_GetNameW(iBoard, BufLen, pNameBuf, pNameLen);
}

inline TWinErrCode DAC_SetRange(TBoardIndex iBoard, TChannelIndex Ch = ciAll, TRangeCode Range = rcBipolar10)
{
	return DAC_SetRange1(iBoard, Ch, Range);
}

inline TWinErrCode DAC_SetRange(TBoardIndex iBoard, TUInt32 RangeCt, TPRangeCode pRange)
{
	return DAC_SetRangeAll(iBoard, RangeCt, pRange);
}

inline TWinErrCode DAC_Output(TBoardIndex iBoard, TChannelIndex Ch, TDouble V)
{
	return DAC_OutputV(iBoard, Ch, V);
}

inline TWinErrCode DAC_Output(TBoardIndex iBoard, TChannelIndex Ch, TUInt64 Counts)
{
	return DAC_OutputCounts(iBoard, Ch, Counts);
}

inline TWinErrCode ADC_SetRange(TBoardIndex iBoard, TChannelIndex Ch = ciAll, TRangeCode RangeCode = rcBipolar10)
{
	return ADC_SetRange1(iBoard, Ch, RangeCode);
}

inline TWinErrCode ADC_SetRange(TBoardIndex iBoard, TUInt32 RangeCt, TPRangeCode pRangeCodes)
{
	return ADC_SetRangeAll(iBoard, RangeCt, pRangeCodes);
}

inline TWinErrCode ADC_GetImmediateScan(TBoardIndex iBoard, TPDouble pVData, TRangeCode Range = rcNoChange)
{
	return ADC_GetImmediateScanV(iBoard, pVData, Range);
}

inline TWinErrCode ADC_GetImmediateScan(TBoardIndex iBoard, TPUInt32 pRawData, TRangeCode Range = rcNoChange)
{
	return ADC_GetImmediateScanRaw(iBoard, pRawData, Range);
}

inline TWinErrCode ADC_GetImmediate(TBoardIndex iBoard, TPDouble V, TChannelIndex Ch, TRangeCode Range = rcNoChange)
{
	return ADC_GetImmediateV(iBoard, V, Ch, Range);
}

inline TWinErrCode ADC_GetImmediate(TBoardIndex iBoard, TPUInt32 Data, TChannelIndex Ch, TRangeCode Range = rcNoChange)
{
	return ADC_GetImmediateRaw(iBoard, Data, Ch, Range);
}

inline TWinErrCode ADC_GetData(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPDouble pData = NULL, TPUInt16 pStatus = NULL, TPUInt16 pTimestamp = NULL)
{
	return ADC_GetDataV(iBoard, DataCt, pGotDataCt, pData, pStatus, pTimestamp);
}

inline TWinErrCode ADC_GetData(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPUInt16 pData = NULL, TPUInt16 pStatus = NULL, TPUInt16 pTimestamp = NULL)
{
	return ADC_GetDataCounts(iBoard, DataCt, pGotDataCt, pData, pStatus, pTimestamp);
}

inline TWinErrCode ADC_GetData(TBoardIndex iBoard, TUInt32 DataCt, TPUInt32 pGotDataCt, TPUInt32 pDataRaw = NULL)
{
	return ADC_GetDataRaw(iBoard, DataCt, pGotDataCt, pDataRaw);
}
#endif /* __cplusplus */

/* C++ spelling shared with the Windows sequencer-free wrappers. */
#ifdef __cplusplus
namespace aioaio {
using ::TUInt8;
using ::TPUInt8;
using ::TUInt16;
using ::TPUInt16;
using ::TUInt32;
using ::TPUInt32;
using ::TUInt64;
using ::TPUInt64;
using ::TUNative;
using ::TPUNative;
using ::TSingle;
using ::TPSingle;
using ::TDouble;
using ::TPDouble;
using ::TAnsiChar;
using ::TPAnsiChar;
using ::TWideChar;
using ::TPWideChar;
using ::TPointer;
using ::TWinErrCode;
using ::TPWinErrCode;
using ::TSerialNumber;
using ::TPSerialNumber;
using ::TLongBool;
using ::TBoardIndex;
using ::TChannelIndex;
using ::TRangeCode;
using ::TPRangeCode;
using ::TIRQIndex;
using ::TChunkingType;
using ::TCallbackType;
using ::TADCCallbackRaw;
using ::TADCCallbackCounts;
using ::TADCCallbackV;
using ::TBoardCapabilities;
using ::TADCAcquisitionStatus;
using ::TDACWaveformCapabilities;
using ::TDACWaveformStatus;
using ::AIO_Initialize;
using ::AIO_Shutdown;
using ::AIO_ErrorString;
using ::BRD_Open;
using ::BRD_Close;
using ::BRD_GetCapabilities;
using ::ADC_GetAcquisitionStatus;
using ::ADC_WaitData;
using ::BRD_GetHighestBoardIndex;
using ::BRD_GetNameA;
using ::BRD_GetNameW;
using ::BRD_GetSerialNumber;
using ::BRD_GetDeviceID;
using ::BRD_Reset;
using ::DAC_SetRange1;
using ::DAC_SetRangeAll;
using ::DAC_OutputV;
using ::DAC_OutputCounts;
using ::DAC_GetWaveformCapabilities;
using ::DAC_StartWaveform;
using ::DAC_GetWaveformStatus;
using ::DAC_StopWaveform;
using ::ADC_SetRange1;
using ::ADC_SetRangeAll;
using ::ADC_SetRate;
using ::ADC_GetRate;
using ::ADC_SetChannelCount;
using ::ADC_SetDifferential;
using ::ADC_GetImmediateScanV;
using ::ADC_GetImmediateScanRaw;
using ::ADC_GetImmediateV;
using ::ADC_GetImmediateRaw;
using ::ADC_ConfigureAcquire;
using ::ADC_AcquireBlock;
using ::ADC_AcquireStream;
using ::ADC_AcquireEnd;
using ::ADC_SetCallback;
using ::ADC_GetDataV;
using ::ADC_GetDataCounts;
using ::ADC_GetDataRaw;
using ::ADC_PollData;
using ::ADC_ClearData;
using ::PIO_In8;
using ::PIO_In16;
using ::PIO_In32;
using ::PIO_Out8;
using ::PIO_Out16;
using ::PIO_Out32;
using ::PIO_INS8;
using ::PIO_INS16;
using ::PIO_INS32;
using ::PIO_OUTS8;
using ::PIO_OUTS16;
using ::PIO_OUTS32;
using ::PIO_WaitForIRQ;
using ::PIO_PollForIRQ;
using ::PIO_AbortIRQWait;
using ::BRD_GetName;
using ::DAC_SetRange;
using ::DAC_Output;
using ::ADC_SetRange;
using ::ADC_GetImmediateScan;
using ::ADC_GetImmediate;
using ::ADC_GetData;
} // namespace aioaio
#endif

#endif /* AIOAIO_LINUX_H_INCLUDED */
