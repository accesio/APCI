# Linux AIOAIO library

`libaioaio` provides a board-scoped C API for production ACCES AxIO ADC boards, immediate DAC output on supported AIO/ADIO/DAAI cards, and timed DAC waveform output on selected FDS boards using the APCI Linux driver. Applications include `AIOAIO.h` and link to `libaioaio`; they do not manage device discovery details, register programming, DMA buffers, IRQ waits, or acquisition workers. The library manages the user-space acquisition path, while the kernel driver owns hardware interrupt handling and DMA allocation. APCI is a private transport dependency.

This is an initial Linux implementation. It has a new ABI: ADC calls have no sequencer argument, integer widths are fixed, and errors are positive Linux `errno` values. It cannot be substituted for the existing Windows DLL. Prototype hardware is excluded. Hardware validation is still required; the automated tests use a fake backend.

The production catalog covers selected mPCIe AIO/AI, PCIe ADIO/ADI, mPCIe ADIO/ADI, and mPCIe DAAI families listed in `src/backend.cpp`. Special DF/HC models and DA-only boards are not supported by this milestone. Catalog inclusion is not a claim of completed hardware qualification or a measured maximum rate.

## Build and check

The intended repository location is `aioaiolib/` beside `apcilib/` in the APCI checkout. Requirements are a C11 compiler, C++17 compiler, GNU make, POSIX threads, and Python 3 for the checks.

```sh
cd aioaiolib
make
make check
./build/check_dma --help
./build/axio_benchmark --help
./build/axiodac --help
./build/axiodac_irq --help
```

When building outside that checkout, identify the APCI source root explicitly:

```sh
make APCI_DIR=/path/to/apci
make APCI_DIR=/path/to/apci check
```

The build creates `build/libaioaio.so`, its versioned library, `build/check_dma`, `build/axio_benchmark`, `build/axiodac`, `build/axiodac_irq`, and `build/aioaio.pc`. All four programs load the adjacent library through its build-time runtime search path. `make check` compiles the C/C++ headers, checks exported symbols, and runs ADC and DAC engine tests against fake backends, plus transport tests with APCI and system calls stubbed. The DAC examples also run against public-API stubs to check waveform layout, exact-path selection, error cleanup, and interruption. These checks do not run the production acquisition path or access a board. `--help` also does not discover or open hardware.

`make install PREFIX=/usr/local` installs the library, header, and pkg-config metadata. `DESTDIR` is supported for staging. Installation does not load a kernel module or modify device permissions. Configure the system library search path as appropriate for the installation prefix.

## Simple DMA example

`examples/check_dma.c` is a short public-API walkthrough: open a board, configure its ADC, start a finite capture, wait for and consume data, check completion, then stop and release resources. Settings are constants at the top of the file: 100,000 scalar results, 10,000 conversions/second per ADC, and a 30-second deadline. It selects all single-ended channels in the bipolar 10 V range, prints completion counters and the first eight raw words, and creates no output file. The capture loop includes a marked location for processing each returned buffer. Ctrl-C stops acquisition through normal cleanup.

```sh
# Discover boards and capture from the first supported board.
./build/check_dma

# Open only this board.
./build/check_dma /dev/apci/mpcie_adi12_8e_0
```

The examples use only the public AIOAIO API for board access. The APCI driver must already be loaded and the selected device must be accessible to the caller. `check_dma --help` does not open hardware. Set `AIOAIO_DEBUG=1` before running the simple example to trace register operations.

## Immediate DAC output

`DAC_SetRange1`, `DAC_SetRangeAll`, `DAC_OutputCounts`, and `DAC_OutputV` implement ordinary DAC output through register writes in userspace. No additional kernel changes are required for these four operations. `BRD_GetCapabilities` appends `DACCount` and `DACResolutionBits`; check the returned `StructSize` before reading them when using an older library. Both fields are zero on ADC-only AI/ADI cards, whose DAC calls return `ENOTSUP`. ADC resolution and DAC resolution are independent.

| Supported production family | DAC | Channels | Ranges |
| --- | --- | --- | --- |
| AIO / ADIO models in the library catalog | LTC2664, 16-bit | 4 | 0–5 V, 0–10 V, ±5 V, ±10 V, ±2.5 V |
| DAAI `-4` models in the library catalog | AD5755, 16-bit | 4 | 0–5 V, 0–10 V, ±5 V, ±10 V; 4–20, 0–20, 0–24 mA |
| DAAI `-8` models in the library catalog | AD5755, 16-bit | 8 | Same AD5755 ranges |

Use named ranges (`rcUnipolar5`, `rcUnipolar10`, `rcBipolar5`, `rcBipolar10`, `rcBipolar2_5`, `rcCurrent420`, `rcCurrent020`, `rcCurrent024`) to avoid chip-dependent numeric codes. Numeric codes 0–3 are the four common voltage ranges in table order; code 4 is ±2.5 V on LTC2664 and 4–20 mA on AD5755; AD5755 codes 5 and 6 are 0–20 and 0–24 mA. Unsupported ranges return `EINVAL`.

For example, after opening a board, check each return value from `DAC_SetRange1(board, 0, rcBipolar10)` and `DAC_OutputV(board, 0, 1.25)` to configure DAC 0 and output 1.25 V. `ciAll` selects all outputs. `DAC_SetRangeAll` takes one range per channel, repeats the final supplied range across remaining channels, and ignores entries beyond the hardware's channel count. Zero count is a successful no-op; `rcNoChange` leaves a channel's range and cached conversion state unchanged.

Set a range before using `DAC_OutputV`. The library does not assume a power-up range: unknown state returns `ENODATA`. Despite its legacy name, `DAC_OutputV` takes **milliamps** in an AD5755 current range. `DAC_OutputCounts` accepts only 0–65535 and does not establish a range. Voltage/current conversion uses 65536 counts per span and rounds to nearest, ties to even. The lower physical endpoint is included; the upper endpoint is excluded, and values rounding above 65535 return `ERANGE`. NaN/infinity return `EINVAL`. For ±10 V, counts 0/32768/65535 mean −10 V/0 V/(10 V minus one LSB).

All selected range/value inputs are checked before the first write. A transport error can leave a request partially applied: successfully configured channels retain their known ranges, a channel whose range write failed becomes unknown, and subsequent channels are untouched. Reconfigure any unknown range before value output. AD5755 range changes disable the channel, preload zero volts (or the current range's lower endpoint), wait at least 1 ms, then enable it; SPI busy waits have a 100 ms timeout. LTC2664 span changes preserve the programmed count.

Immediate operations return `EBUSY` while ADC acquisition or DAC waveform playback owns the board. A nonzero DAC divider on FDS or AD5755 models also returns `EBUSY`; older non-FDS LTC2664 maps used that address for V/I detection and are not interpreted as a running divider. External LDAC mode returns `ENOTSUP`, as does an external waveform trigger on supported FDS models. Waveform setup/stop establishes ±10 V for every output and keeps the immediate conversion state synchronized. Immediate output values otherwise persist across close/shutdown. Do not use an independent register writer while this library owns a board.

DA-only and prototype cards remain excluded. Automated tests cover both command protocols, current/voltage/count boundaries, array and broadcast validation, partial failures, SPI timeouts, capability ABI compatibility, and transitions between immediate output and waveform ownership without accessing hardware. Analog accuracy, settling, and each physical board/firmware combination still require hardware verification.

## Timed DAC waveform examples

`examples/axiodac.c` and `examples/axiodac_irq.c` demonstrate repeating output through four public calls: `DAC_GetWaveformCapabilities`, `DAC_StartWaveform`, `DAC_GetWaveformStatus`, and `DAC_StopWaveform`. Their settings are constants at the top of each file. Both run for five seconds or until Ctrl-C, then stop and close their board. They open the exact supplied path, defaulting to `/dev/apci/mpcie_aio16_16fds_0`; they do not discover an arbitrary first card or fall back when an explicit path fails.

```sh
# Two-channel square wave and ramp, refilled by polling.
./build/axiodac

# One-channel cosine, refilled using DAC FIFO interrupts.
./build/axiodac_irq /dev/apci/mpcie_aio16_16fds_0
```

The waveform layout and rates match the low-level `apcilib/axiodac*.c` samples. `axiodac` alternates channel 0 between 0 and 65535 on each point and advances channel 1's ramp by one count every two points. Its buffer contains 131,072 two-channel points, played at 500,000 points/s. `axiodac_irq` contains one cosine cycle in 262,144 one-channel points, played at 1,000,000 points/s. Both therefore request 1,000,000 scalar DAC samples/s. One point supplies one sample to every selected output channel; the aggregate scalar rate is `PointRateHz * ChannelCount`. The status reports the actual clock-divider point rate.

Timed DAC support is limited to these production FDS models:

| PCI device ID | Model |
| --- | --- |
| `0xc0eb` | mPCIe-AIO16-16FDS |
| `0xc2ef` | PCIe-ADIO16-16FDS |
| `0xc0ef` | mPCIe-ADIO16-8FDS |

Initialize `TDACWaveformCapabilities.StructSize` before querying a board. The query does not change DAC outputs; unsupported boards, including ADC-only cards, return `ENOTSUP`. Check the returned channel count, FIFO capacity, and aggregate `MaxSampleRateHz`. Prototype hardware remains excluded.

`DAC_StartWaveform` copies the caller's interleaved 16-bit counts for channels `0..ChannelCount-1`. `SampleCt` is the total scalar sample count and must contain complete points. The library adds channel tags, configures the bipolar 10 V range, and sets all DAC outputs to zero volts before playback. In that range, 0 is negative full scale, 32768 is zero volts, and 65535 is positive full scale minus one LSB. The copied waveform repeats until stopped; callers may reuse their input buffer after a successful start.

Rebuild and load the updated `apci.ko` before using the DAC buffer path; the accompanying driver changes add allocation and transfer-bound checks. The library manages the mapped host buffer, FIFO uploads, and refill worker. **This path uses CPU-driven FIFO uploads through APCI, not PCIe DAC DMA.** `AIO_DAC_REFILL_POLL` polls FIFO capacity; `AIO_DAC_REFILL_IRQ` waits for FIFO refill interrupts. The examples' foreground loops only check status at bounded intervals. Status counters measure host uploads, refill operations, and IRQ wakeups; uploaded samples can still be queued and must not be described as samples physically played. Hardware timing, uninterrupted maximum-rate operation, and analog settling still require board testing.

`DAC_StopWaveform` stops output, joins the worker, and restores zero volts. Close/shutdown also stop playback. An underrun is reported as `EPIPE`; stop may return an earlier latched playback failure. ADC capture and DAC playback cannot run concurrently on the same board and return `EBUSY` if the other operation is active. Immediate DAC operations also return `EBUSY` during playback; see the range-state rules above.

In IRQ mode, status queries request an asynchronous FIFO check by waking the refill worker. Repeated status polling therefore detects an empty FIFO even if its interrupt was missed. Stop also checks for an underrun before clearing the FIFO. Applications should monitor status during playback, as these examples do.

Existing external LDAC or external waveform-trigger modes return `ENOTSUP`; this API uses the internal point clock. A divider already running outside this library returns `EBUSY`. Stopping the divider clears the DAC FIFO. Cleaning a FIFO that was preloaded before the clock started requires the documented DAC-only reset, which also clears an old LDAC event latch; it does not reset ADC or general DIO state. All four DAC ranges and outputs are owned by waveform playback, including unused channels.

The register contract was checked against the current local FPGA sources and register maps: mPCIe-AIO16-16F release 5.7 (22 September 2026), PCIe-ADIO16-16F and mPCIe-ADIO16-8F FDS implementations. In particular, status bit 29 means FIFO empty, divisor zero stops playback, and channel count is 1–4. The older samples' `WaveformPlaying` name does not describe that bit's polarity.

The driver buffer checks can be exercised without loading a module with `python3 ../tests/test_dac_buffer_guards.py` from this directory. They inject allocation failures and check mapping/transfer bounds using AddressSanitizer and UndefinedBehaviorSanitizer. The allocation remains device-wide: raw clients must coordinate ownership and unmap before resizing. The checks do not make concurrent resizing by uncooperative clients safe.

## Capture and benchmark utility

The previous configurable `check_dma` has moved to `examples/axio_benchmark.c` and builds as `axio_benchmark`. Its capture, CSV, benchmark, discovery, and diagnostic options are retained.

```sh
# Discover supported, accessible boards.
./build/axio_benchmark --list

# Capture 100,000 valid ADC results into a CSV file.
./build/axio_benchmark --device /dev/apci/pcie_adio16_16fds_0 \
  --samples 100000 --rate 10000 --timeout 30 --csv samples.csv

# Capture approximately two seconds of results with no CSV output.
./build/axio_benchmark --board 0 --seconds 2 --rate 100000 --timeout 10 --benchmark

# Select differential mode and a total channel count supported by the board.
./build/axio_benchmark --board 0 --differential --channels 4 \
  --samples 10000 --rate 10000 --timeout 10 --benchmark
```

For `axio_benchmark`, the default output is `samples.csv`; an existing file is overwritten. `--benchmark` disables file creation and all per-batch output, and cannot be combined with `--csv`. `--samples` and `--seconds` are mutually exclusive. The default capture is 100,000 results, 10,000 conversions/second per ADC, all single-ended channels, and a 30-second deadline. The utility selects the bipolar 10 V range. A duration is converted to a scalar result count using the actual rate returned by the library, rounded upward; the count must fit in 32 bits.

Each CSV row contains `sample_index,raw_word`. The index starts at zero; the raw word is an eight-digit hexadecimal ADC result with its hardware metadata intact. Only valid ADC results count toward the requested total. A finite request may end partway through a scan. The example does not decode words using private register definitions or assume a fixed number of ADCs.

The summary reports requested, captured, and delivered counts; selected channels; terminal state; DMA completions/bytes; discarded DMA slots; and any latched acquisition error. A count mismatch, data loss, timeout, write failure, or hardware/API failure produces a nonzero exit status. A signal handler only sets a flag; the main loop stops acquisition and releases resources. The wait interval is at most 100 ms so cancellation remains responsive while waiting for data.

Benchmark timing measures monotonic wall time, process CPU time, first-data delivery latency, and aggregate host delivery throughput. It starts immediately before `ADC_AcquireBlock` and stops after the read loop, before final cleanup. Process CPU time includes library workers. These are host/API measurements, not hardware conversion timing, jitter, or percentile latency. CSV mode includes formatting and buffered writes in the loop but excludes the final file close/flush, so its throughput is not a durable-disk benchmark. The overall deadline includes time spent writing CSV.

If `axio_benchmark` fails during startup, rerun the same command with `--debug`. This enables the library's `AIOAIO_DEBUG=1` diagnostics on standard error, including the raw FPGA revision, hardware setup operations, and expected versus actual register readbacks. For example:

```sh
./build/axio_benchmark --device /dev/apci/mpcie_adi12_8e_0 --debug
```

Other applications can enable the same diagnostics with the `AIOAIO_DEBUG=1` environment variable before opening a board. Debug output adds timing overhead; leave it disabled for performance measurements. If shutdown returns the acquisition error already recorded in board status, the example labels it as the earlier acquisition failure rather than presenting it as an unexplained second failure. Different shutdown errors are reported separately.

## ADC rate limits

The model catalog uses the following published maximum rates per physical ADC. Two-ADC cards can produce twice the listed aggregate scalar result rate; selecting fewer channels does not raise the ADC's maximum conversion rate.

| Model variant | Maximum conversions/s/ADC |
| --- | ---: |
| 16-bit F / FDS | 1,000,000 |
| 16-bit A | 500,000 |
| 16-bit E | 250,000 |
| 12-bit A | 500,000 |
| 12-bit without a suffix | 250,000 |
| 12-bit E | 100,000 |

These ratings are listed in the [mPCIe-AIO family](https://accesio.com/product/mpcie-aio16-16f/), [PCIe-ADIO family](https://accesio.com/product/pcie-adio16-16f/), and [mPCIe-DAAI family](https://accesio.com/product/mpcie-daai16-8f/) product documentation. `axio_benchmark --list` displays the limit supplied by the library for each discovered card. For example, requesting 250,000 conversions/s/ADC from an mPCIe-ADI12-8E reports its 100,000 conversions/s/ADC (100 ksps) maximum and exits before DMA setup or CSV creation.

DAAI hardware qualification is still pending: some existing DAAI device-ID/name mappings differ from the Rev 1.4 FPGA rate decoder, including CE50/CE51/CE52 and the 16-bit four-output variants. The catalog retains the existing model identities and their published ratings; it does not establish that every firmware revision implements those rates. That discrepancy needs confirmation before changing those mappings.

## API contracts

- **Discovery and ownership:** loading the library does not access hardware. `AIO_Initialize` discovers `/dev/apci`; `BRD_Open` opens one exact path without requiring discovery. Discovery retains cooperative locks on opened boards until close/shutdown. An exact path is useful when other boards should remain available. `BRD_Close` and `AIO_Shutdown` stop acquisition and release the library's resources. Cooperative locks do not prevent programs that bypass this library from touching hardware.
- **Capabilities:** initialize `TBoardCapabilities.StructSize` before calling `BRD_GetCapabilities`. Use the returned ADC count, channel capacity, resolution, DMA availability, and `MaxADCRateHz` instead of guessing from a product name. The implementation maintains an explicit production-device catalog in its private backend.
- **Channels:** `ADC_SetChannelCount` takes the total number of selected channels, divided equally among the board's ADCs. Differential mode halves the maximum channels per ADC. Set the mode before setting the channel count. Physical channel identities are retained; dual-ADC raw data keeps simultaneous-pair ordering, such as `0, 8, 1, 9, ...` for a full single-ended scan. Partial selections and differential mode must follow the board's supported geometry.
- **Rate:** `ADC_SetRate` requests conversions per second **per physical ADC**, shared among that ADC's selected channels. `ADC_GetRate` returns the actual clock-divider rate. `ADC_SetRate` rejects requests above the model's `MaxADCRateHz` with `ERANGE`; clock-divider rounding also respects that ceiling. Both ADC examples report the card name, requested rate, and maximum before setting up a capture. The limit is per physical ADC, not per channel or per board. The scalar result rate is `actual_rate * ADCCount`; the per-channel rate is `actual_rate / selected_channels_per_ADC`. Both ADCs on a dual-ADC board share timing. For example, two ADCs each converting at 10,000/s produce 20,000 scalar results/s; with eight selected channels per ADC, each channel is sampled at 1,250/s.
- **Counts and buffers:** `SampleCt`, `DataCt`, callback counts, and delivered/captured counters refer to valid scalar ADC results, not scans, FIFO rows, or DMA bytes. `ADC_AcquireBlock` delivers exactly its requested count on success, ignoring final DMA padding. Caller buffers for getters need room for `DataCt` elements of the relevant type. Raw, counts, and voltage getters consume the same queue; callers should choose one format for each portion of the stream.
- **State and waits:** getters are nonblocking. They return zero when data was delivered, `EAGAIN` for running/empty, `ENODATA` for complete/drained, and `ECANCELED` for cancelled/drained. `ADC_WaitData` waits for data or a terminal state; `ETIMEDOUT` does not itself cancel acquisition. Initialize `TADCAcquisitionStatus.StructSize` before retrieving status. Completion is reported only after the backend has stopped. Check latched errors and discarded-slot counters as well as delivered data.
- **Failure and shutdown:** configuration changes during active acquisition are rejected. Finite captures and stream captures use bounded buffering; overflow is an error rather than silent replacement of old results. Stop acquisition before closing or restarting. The sample verifies the final state and counters instead of treating an empty read as successful completion.
- **Errors:** `TWinErrCode` retains its historical name but contains zero or a positive Linux error number. Use `AIO_ErrorString` for readable diagnostics. Do not compare returned values with Windows error constants.

The library implements discovery, capabilities, ADC configuration, finite/stream DMA acquisition, queued or callback delivery, immediate ADC reads, board reset, scalar/block register I/O, immediate DAC output, and FDS waveform playback. The common ADC functions are also available through the C++ `aioaio::` namespace on Linux and the updated Windows headers; the timed DAC waveform API is Linux-specific. Timestamp output arrays contain zero because this acquisition format has no hardware timestamp.

### Immediate ADC reads

`ADC_GetImmediateRaw` and `ADC_GetImmediateV` return one selected input; `ADC_GetImmediateScanRaw` and `ADC_GetImmediateScanV` return a complete scan in the configured single-ended/differential mode. These operations require idle ADC and DAC workers. They use software-started conversions with bounded waits and restore the saved ADC controls; an optional range override applies only to that call. `rcNoChange` uses the library's configured ranges. They clear the hardware ADC FIFO, but do not consume previously queued DMA results or change the configured streaming rate/channel count.

Allocate `MaxChannels` elements (8 or 16) for scan output. Results are indexed by board channel: single-ended channels 0–7 and, when present, 8–15. Differential scans use pair indices 0–3 and 8–11, corresponding to physical input pairs 0/1, 2/3, 4/5, 6/7 on each ADC; unused slots contain raw `0x80000000` or voltage `NaN`. Single-input calls use those same indices. The raw word retains its hardware channel/status tags. DMA acquisitions continue to preserve native simultaneous-pair ordering rather than this indexed scan layout.

Software-started scans are sequential and do not promise simultaneous conversions across both ADCs. Firmware qualification remains necessary: ADIO FPGA release 4.5 notes an immediate-mode FIFO correction, so older firmware must not be assumed equivalent merely because streaming DMA works.

The existing `ADC_SetRange1/All` configuration still uses physical input indices. Differential pair indices 0–3 therefore take their configured ranges from entries 0, 2, 4, 6; ADC1 uses entries 8, 10, 12, 14. Passing an explicit `Range` to an immediate read overrides all inputs for that call only.

### ADC callbacks and chunk sizing

Register `ctRaw`, `ctCounts`, or `ctV` with a matching nonnull callback while idle and after draining or clearing any queued data. `ADC_SetCallback(board, ctNone, NULL)` restores queued delivery. Registration persists across captures and board reset. Callback mode delivers each batch directly instead of retaining a second copy in the queue; `ADC_GetData*`, `ADC_WaitData`, and `ADC_PollData` return `EBUSY` while a callback is registered. Status counters report captured and delivered scalar results.

Callbacks run on the acquisition worker, without library locks held. Their arrays are borrowed until the call returns; copy values needed later. Counts, status and volts follow native DMA ordering; timestamps are zero. A slow callback can still allow the DMA ring to overflow, which is reported rather than hidden.

`ADC_GetAcquisitionStatus` and board identity/capability queries may be called inside a callback. Calling `ADC_AcquireEnd` on the callback's own board requests a deferred stop and returns zero; the resulting state is `CANCELLED`, even if this was the final batch. Other control/lifecycle operations return `EDEADLK` from callback context before taking locks. External stop, close and shutdown join the worker and wait for an in-flight callback. A C++ exception escaping a callback terminates capture with `EIO`.

`ADC_ConfigureAcquire` supports `ctAuto` (1024 FIFO rows, within the model limit), `ctSize` (raw word count rounded up to whole FIFO rows), and `ctRate` (requested batches per second). Rate-based sizing uses `ceil(actual_conversion_rate * ADCCount / (Chunk * words_per_FIFO_row))`, at least one row. If that exceeds the hardware FIFO limit, it returns `ERANGE`. Sizing is recalculated at capture start so subsequent rate changes apply. Whole rows and finite-capture shortening make the actual callback rate approximate; the final batch can be partial. These are delivery controls, not sampling-jitter guarantees.

### Reset and raw register access

`BRD_Reset` stops and joins active workers, resets the hardware, clears queued results, and on success restores idle status and software ADC defaults (single-ended, all channels, bipolar 10 V, 10,000 conversions/s/ADC, automatic chunk sizing). It preserves callback registration. A successful reset recovers from an earlier acquisition/playback failure; a failed reset reports its error. Hardware reset can change DAC and DIO outputs. Cached DAC ranges become unknown, so configure a range again before `DAC_OutputV`.

`PIO_In8/16/32`, `PIO_Out8/16/32`, `PIO_INS8/16/32`, and `PIO_OUTS8/16/32` access byte offsets in **logical BAR[1]**, including on boards whose physical PCI resource is BAR2. The supported native aperture is 4 KiB. Offsets must align to the transfer width and fit entirely within that aperture. Use the width required by the hardware register map; native control registers generally require 32-bit accesses.

Scalar reads return zero on failure and set `errno`; they clear `errno` on success. Other operations return zero or a positive errno. Block counts are elements rather than bytes, and all elements access the **same register offset**, as required for FIFO/port reads and writes. These userspace loops use the ordinary APCI scalar transport and do not resize or map the kernel DAC staging buffer. A failed block may have transferred a prefix; this API has no completed-count output, so retrying the whole block can repeat side effects. Nonzero counts require a caller-owned buffer of sufficient size.

Raw I/O requires idle library workers. Raw writes invalidate cached DAC ranges and ADC backend configuration; the high-level ADC configuration is reapplied before the next capture. Raw I/O is intended for deliberate register operations, not concurrent manipulation of an active managed acquisition.

Raw writes that enable ADC-trigger or DMA-done interrupts (`+0x40`, bits 0 and 2) return `EBUSY` unless this library has prepared a DMA ring. The current driver's ISR assumes that ring exists; raw interrupt setup must not make it use an unallocated ring. After a failed board reset, managed configuration and raw writes remain blocked until a successful reset; diagnostic reads remain available.

### Remaining unsupported APIs

`BRD_GetSerialNumber` is **deprecated and intentionally unsupported**, not scheduled for implementation. It remains exported for ABI compatibility, returns `ENOTSUP`, and writes zero when given a nonnull result pointer. The header emits a deprecation diagnostic on supported compilers.

`PIO_WaitForIRQ`, `PIO_PollForIRQ`, and `PIO_AbortIRQWait` remain explicit `ENOTSUP` stubs. Their implementation was assessed but is not included in this change:

- The current APCI driver already latches a pending IRQ token, preventing an event between checking DMA readiness and entering a blocking wait from being lost. Blocking wait and cancellation transport exist.
- There is no nonblocking operation to consume that token, nor a file `poll` implementation. Reading the hardware status register cannot implement `PIO_PollForIRQ`: the ISR clears status, and the Windows function consumes a driver-maintained pending event.
- A complete implementation should add an atomic, nonblocking pending-event consume operation returning `EAGAIN` when empty; optional `poll`/`epoll` support can use the same state. It should check existing waiter ownership before consuming pending events and define per-file ownership/cancellation rather than allowing another open file to interfere.
- The library would need one explicit IRQ owner per board, reject direct waits during ADC/DAC worker ownership, validate `iiOnly`, and coordinate cancellation with close/reset. Cancellation before a wait actually enters the driver currently returns `EALREADY`, so a handshake or repeated cancellation is still necessary. Wait functions should not silently configure or acknowledge hardware interrupt sources.
- The ISR's ADC DMA paths also need validation of allocated ring state before using DMA addresses or taking a modulo by slot count. This is a prerequisite for exposing arbitrary direct IRQ sources safely, not a performance optimization.

No direct-IRQ or kernel implementation changes are made here.

Do not run raw APCI clients against a board owned by this library, or use raw PIO to configure acquisition alongside the high-level ADC API. Cooperative locks cannot prevent those clients from changing shared hardware or DMA state. The existing APCI driver DMA resize path has a race limitation that this user-space library does not fix; repeated capture, stop/start, and resize behavior require validation on the target hardware. Neither the example timings nor the fake-backend tests establish those hardware guarantees.

The original `apcilib/check_dma.c` remains a low-level driver reference; the programs in `aioaiolib/examples/` exercise the public library boundary. The original `apcilib/axiodac.c` and `apcilib/axiodac_irq.c` remain low-level DAC references.
