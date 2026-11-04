# Linux AIOAIO library

`libaioaio` provides a board-scoped C API for production ACCES AxIO ADC boards using the APCI Linux driver. Applications include `AIOAIO.h` and link to `libaioaio`; they do not manage device discovery details, register programming, DMA buffers, IRQ waits, or acquisition workers. The library manages the user-space acquisition path, while the kernel driver owns hardware interrupt handling and DMA allocation. APCI is a private transport dependency.

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
```

When building outside that checkout, identify the APCI source root explicitly:

```sh
make APCI_DIR=/path/to/apci
make APCI_DIR=/path/to/apci check
```

The build creates `build/libaioaio.so`, its versioned library, `build/check_dma`, `build/axio_benchmark`, and `build/aioaio.pc`. Both programs load the adjacent library through its build-time runtime search path. `make check` compiles the C/C++ headers, checks exported symbols, and runs acquisition tests against the fake backend. These checks do not run the production acquisition path or access a board. `--help` also does not discover or open hardware.

`make install PREFIX=/usr/local` installs the library, header, and pkg-config metadata. `DESTDIR` is supported for staging. Installation does not load a kernel module or modify device permissions. Configure the system library search path as appropriate for the installation prefix.

## Simple DMA example

`examples/check_dma.c` is a short public-API walkthrough: open a board, configure its ADC, start a finite capture, wait for and consume data, check completion, then stop and release resources. Settings are constants at the top of the file: 100,000 scalar results, 10,000 conversions/second per ADC, and a 30-second deadline. It selects all single-ended channels in the bipolar 10 V range, prints completion counters and the first eight raw words, and creates no output file. The capture loop includes a marked location for processing each returned buffer. Ctrl-C stops acquisition through normal cleanup.

```sh
# Discover boards and capture from the first supported board.
./build/check_dma

# Open only this board.
./build/check_dma /dev/apci/mpcie_adi12_8e_0
```

Both examples use only the public AIOAIO API for board access. The APCI driver must already be loaded and the selected device must be accessible to the caller. `check_dma --help` does not open hardware. Set `AIOAIO_DEBUG=1` before running the simple example to trace register operations.

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
- **Rate:** `ADC_SetRate` requests conversions per second **per physical ADC**, shared among that ADC's selected channels. `ADC_GetRate` returns the actual clock-divider rate. `ADC_SetRate` rejects requests above the model's `MaxADCRateHz` with `ERANGE`; clock-divider rounding also respects that ceiling. Both examples report the card name, requested rate, and maximum before setting up a capture. The limit is per physical ADC, not per channel or per board. The scalar result rate is `actual_rate * ADCCount`; the per-channel rate is `actual_rate / selected_channels_per_ADC`. Both ADCs on a dual-ADC board share timing. For example, two ADCs each converting at 10,000/s produce 20,000 scalar results/s; with eight selected channels per ADC, each channel is sampled at 1,250/s.
- **Counts and buffers:** `SampleCt`, `DataCt`, callback counts, and delivered/captured counters refer to valid scalar ADC results, not scans, FIFO rows, or DMA bytes. `ADC_AcquireBlock` delivers exactly its requested count on success, ignoring final DMA padding. Caller buffers for getters need room for `DataCt` elements of the relevant type. Raw, counts, and voltage getters consume the same queue; callers should choose one format for each portion of the stream.
- **State and waits:** getters are nonblocking. They return zero when data was delivered, `EAGAIN` for running/empty, `ENODATA` for complete/drained, and `ECANCELED` for cancelled/drained. `ADC_WaitData` waits for data or a terminal state; `ETIMEDOUT` does not itself cancel acquisition. Initialize `TADCAcquisitionStatus.StructSize` before retrieving status. Completion is reported only after the backend has stopped. Check latched errors and discarded-slot counters as well as delivered data.
- **Failure and shutdown:** configuration changes during active acquisition are rejected. Finite captures and stream captures use bounded buffering; overflow is an error rather than silent replacement of old results. Stop acquisition before closing or restarting. The sample verifies the final state and counters instead of treating an empty read as successful completion.
- **Errors:** `TWinErrCode` retains its historical name but contains zero or a positive Linux error number. Use `AIO_ErrorString` for readable diagnostics. Do not compare returned values with Windows error constants.

The milestone implements discovery, capabilities, ADC configuration, finite/stream DMA acquisition, polling/waiting, queued raw/counts/voltage reads, and acquisition status. Immediate-conversion APIs, DAC operations, reset, serial-number retrieval, callback delivery, and status-returning PIO entry points return `ENOTSUP`; their declarations do not imply hardware support. The legacy scalar `PIO_In8/16/32` functions instead return zero and set `errno` to `ENOTSUP`. `ADC_ConfigureAcquire` supports automatic sizing and explicit chunk size; chunk-rate mode remains unsupported. Timestamp output arrays contain zero because this acquisition format has no hardware timestamp. Callback typedefs reserve the future board-scoped interface. Windows adapters that supply the old sequencer value internally are future work.

Do not run raw APCI clients against a board owned by this library, or use raw PIO to configure acquisition alongside the high-level ADC API. Cooperative locks cannot prevent those clients from changing shared hardware or DMA state. The existing APCI driver DMA resize path has a race limitation that this user-space library does not fix; repeated capture, stop/start, and resize behavior require validation on the target hardware. Neither the example timings nor the fake-backend tests establish those hardware guarantees.

The original `apcilib/check_dma.c` remains a low-level driver reference; the two programs in `aioaiolib/examples/` exercise the public library boundary.
