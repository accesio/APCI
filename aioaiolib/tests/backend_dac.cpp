#include "backend.hpp"
#include "AIOAIO.h"
#include "apcilib.h"

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// All device operations are intercepted. This test cannot open real hardware.
namespace {
struct Write { int offset; uint32_t value; };
struct Hardware {
    unsigned id = 0xc0eb;
    std::map<int, uint32_t> registers{{0, 0x400}, {0x08, 0}, {0x0c, 50000000},
        {0x40, 0x054004c0}, {0x48, 1u << 21}, {0x50, 0}, {0x58, 8192}, {0x68, 0x00051195}};
    std::vector<Write> writes;
    std::vector<uint32_t> staging, uploaded;
    std::vector<uint32_t> commands;
    size_t mapping_bytes = 0;
    int maps = 0, unmaps = 0, allocations = 0, closes = 0, buffer_writes = 0;
    int fail_read = -1, fail_write = -1;
    int command_attempts = 0, fail_command = -1, fail_status_after_command = -1;
    bool fail_map = false, fail_allocation = false, fail_buffer = false;
    bool fail_unmap = false, busy = false, fail_stop = false, bad_start = false;
    bool bad_channels = false, reset_works = true, mapped = false;
} h;

std::unique_ptr<aioaio::Backend> open_test() {
    std::unique_ptr<aioaio::Backend> result;
    assert(aioaio::open_backend("/dev/apci/dac-test", result) == 0);
    return result;
}

void prepare(aioaio::Backend &b, uint32_t channels = 2, bool irq = true) {
    double actual = 0;
    assert(b.dac_prepare(channels, 1000000.0 / channels, irq, actual) == 0);
    assert(actual == 1000000.0 / channels);
    assert(h.registers[0x08] == 0);
    assert(h.registers[0x54] == channels);
    assert(h.commands.size() >= 2);
    assert(h.commands[h.commands.size() - 2] == 0xe00003);
    assert(h.commands.back() == 0xa08000);
}

std::vector<uint32_t> waveform(uint32_t channels, uint32_t frames) {
    std::vector<uint32_t> words;
    for (uint32_t frame = 0; frame < frames; ++frame)
        for (uint32_t ch = 0; ch < channels; ++ch)
            words.push_back((ch << 16) | ((frame * 977u + ch) & 0xffffu));
    return words;
}

void require_preserved() {
    assert((h.registers[0x40] & 0xffffu) == 0x4c0); // LDAC, DIO, empty IRQ enables.
    bool reset = false;
    for (const auto &w : h.writes) if (w.offset == 0 && (w.value & 8)) reset = true;
    // DAC-only reset also clears the old LDAC event latch (bit22), as the HDL
    // specifies. DIO and FIFO-empty event statuses are not acknowledged.
    assert((h.registers[0x40] & 0xffff0000u) == (reset ? 0x05000000u : 0x05400000u));
    assert(h.registers[0x48] == (1u << 21));
    assert(h.registers[0] == 0x400);
    for (const auto &w : h.writes) {
        if (w.offset == 0) assert(w.value == 0x400 || w.value == 0x408);
        if (w.offset == 0x40) assert((w.value & 0xffff0000u) == (1u << 25));
        assert(w.offset != 0x10 && w.offset != 0x18 && w.offset != 0x20 && w.offset != 0x38);
    }
}
} // namespace

extern "C" {
int __wrap_open(const char *path, int, ...) {
    assert(std::string(path) == "/dev/apci/dac-test"); return 123;
}
int __wrap_fstat(int fd, struct stat *st) {
    assert(fd == 123); *st = {}; st->st_mode = S_IFCHR; return 0;
}
int __wrap_flock(int fd, int) { assert(fd == 123); return 0; }
int __wrap_close(int fd) { assert(fd == 123); ++h.closes; return 0; }
void *__wrap_mmap(void *, size_t length, int protection, int flags, int fd, off_t offset) {
    assert(fd == 123 && offset == ::sysconf(_SC_PAGESIZE));
    assert(protection == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED);
    assert(length == h.mapping_bytes && !h.mapped);
    if (h.fail_map) { errno = ENOMEM; return MAP_FAILED; }
    ++h.maps; h.mapped = true; return h.staging.data();
}
int __wrap_munmap(void *address, size_t length) {
    assert(h.mapped && address == h.staging.data() && length == h.mapping_bytes);
    if (h.fail_unmap) { errno = EIO; return -1; }
    ++h.unmaps; h.mapped = false; return 0;
}
int apci_get_device_info(int fd, unsigned long index, unsigned int *id, unsigned long *) {
    assert(fd == 123 && index == 0); *id = h.id; return 0;
}
int apci_read8(int, unsigned long, int, int, __u8 *) { assert(false); return -1; }
int apci_read16(int, unsigned long, int, int, __u16 *) { assert(false); return -1; }
int apci_write16(int, unsigned long, int, int, __u16) { assert(false); return -1; }
int apci_read32(int fd, unsigned long index, int bar, int offset, __u32 *value) {
    assert(fd == 123 && index == 0 && bar == 1);
    if (h.fail_read == offset || (offset == 4 && h.command_attempts == h.fail_status_after_command)) { errno = ENXIO; return -1; }
    if (offset == 4) *value = (h.busy ? 1u << 31 : 0) | (h.registers[0x50] == 0 ? 1u << 29 : 0);
    else *value = h.registers[offset];
    if (offset == 0x54 && h.bad_channels) *value = 0;
    return 0;
}
int apci_write32(int fd, unsigned long index, int bar, int offset, __u32 value) {
    assert(fd == 123 && index == 0 && bar == 1);
    h.writes.push_back({offset, value});
    if (h.fail_write == offset || (offset == 8 && !value && h.fail_stop)) {
        h.fail_write = -1; errno = EIO; return -1;
    }
    if (offset == 4) {
        if (++h.command_attempts == h.fail_command) { errno = EIO; return -1; }
        h.commands.push_back(value); return 0;
    }
    if (offset == 0x40) {
        h.registers[offset] = (value & 0xffffu) | ((h.registers[offset] & 0xffff0000u) & ~value);
    } else if (offset == 8) {
        if (value == 0 && h.registers[offset]) h.registers[0x50] = 0;
        h.registers[offset] = value && h.bad_start ? value + 1 : value;
    } else if (offset == 0 && (value & 8)) {
        assert(value == 0x408); // Never SYS/ADC/DIO reset.
        if (h.reset_works) h.registers[0x50] = 0;
        h.registers[8] = h.registers[0x54] = 0;
        h.registers[0x48] &= ~((1u << 20) | (1u << 21));
        h.registers[0x40] &= ~(0x640u | (1u << 22)); // DAC enables and LDAC latch.
        h.registers[offset] = value;
    } else h.registers[offset] = value;
    return 0;
}
int apci_dac_buffer_size(int fd, unsigned long bytes) {
    assert(fd == 123 && !h.mapped); // Resizing a mapped kernel buffer is unsafe.
    assert(bytes >= 65536 && bytes % static_cast<unsigned long>(::sysconf(_SC_PAGESIZE)) == 0);
    ++h.allocations;
    if (h.fail_allocation) { errno = ENOMEM; return -1; }
    h.mapping_bytes = bytes; h.staging.assign(bytes / 4, 0); return 0;
}
int apci_writebuf32(int fd, unsigned long index, int bar, int offset, unsigned int source, int count) {
    assert(fd == 123 && index == 0 && bar == 1 && offset == 0x50 && source == 0);
    assert(count > 0 && h.mapped && static_cast<size_t>(count) * 4 <= h.mapping_bytes);
    assert(static_cast<uint32_t>(count) <= h.registers[0x58] - h.registers[0x50]);
    ++h.buffer_writes;
    if (h.fail_buffer) { errno = EFAULT; return -1; }
    h.uploaded.insert(h.uploaded.end(), h.staging.begin(), h.staging.begin() + count);
    h.registers[0x50] += count; return 0;
}
int apci_write8(int, unsigned long, int, int, __u8) { assert(false); return -1; }
int apci_dma_transfer_size(int, unsigned long, __u8, size_t) { assert(false); return -1; }
int apci_dma_data_ready(int, unsigned long, int *, int *, int *) { assert(false); return -1; }
int apci_dma_data_done(int, unsigned long, int) { assert(false); return -1; }
int apci_wait_for_irq(int, unsigned long) { assert(false); return -1; }
int apci_cancel_irq(int, unsigned long) { assert(false); return -1; }
}

static void check_capabilities_and_validation() {
    for (unsigned id : {0xc0ebu, 0xc2efu, 0xc0efu}) {
        h = Hardware{}; h.id = id;
        auto b = open_test();
        aioaio::DacCapabilities c{};
        assert(b->dac_capabilities(c) == 0);
        assert(c.channels == 4 && c.fifo_samples == 8192 && c.firmware_revision == 0x51195);
        assert(c.max_sample_rate == 1000000);
        double actual = 1;
        assert(b->dac_prepare(0, 1, false, actual) == EINVAL && actual == 0);
        assert(b->dac_prepare(5, 1, false, actual) == EINVAL);
        for (double rate : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()})
            assert(b->dac_prepare(1, rate, false, actual) == EINVAL);
        assert(b->dac_prepare(4, 250000.1, false, actual) == ERANGE);
        assert(b->dac_prepare(1, 1e-10, false, actual) == ERANGE);
        assert(h.writes.empty() && !h.allocations);
        h.registers[0x58] = 0;
        assert(b->dac_capabilities(c) == ENOTSUP && c.channels == 0);
        h.registers[0x58] = 0x4000;
        assert(b->dac_capabilities(c) == ENOTSUP);
        h.registers[0x58] = 8192; h.fail_read = 0x68;
        assert(b->dac_capabilities(c) == ENXIO);
    }
    h = Hardware{}; h.id = 0xc0e8;
    auto b = open_test();
    aioaio::DacCapabilities c{};
    double actual = 0; uint32_t queued = 0; bool empty = false;
    assert(b->dac_capabilities(c) == ENOTSUP);
    assert(b->dac_prepare(1, 1, false, actual) == ENOTSUP);
    assert(b->dac_upload(nullptr, 0) == ENOTSUP);
    assert(b->dac_start() == ENOTSUP && b->dac_state(queued, empty) == ENOTSUP);
    assert(b->dac_stop() == 0 && h.writes.empty() && !h.allocations);
}

static void check_playback_and_cleanup() {
    h = Hardware{};
    {
        auto b = open_test();
        prepare(*b);
        assert(b->dac_start() == EAGAIN);
        auto words = waveform(2, 4096);
        assert(b->dac_upload(words.data(), words.size()) == 0 && h.uploaded == words);
        assert(b->dac_start() == 0 && h.registers[8] == 100);
        assert((h.registers[0x40] & 0xffffu) == 0x6c0);
        uint32_t queued = 0; bool empty = true;
        assert(b->dac_state(queued, empty) == 0 && queued == 8192 && !empty);
        double actual = 0;
        assert(b->dac_prepare(2, 500000, true, actual) == EBUSY);
        assert(b->dac_start() == EBUSY);
        assert(b->dac_upload(words.data(), 2) == ENOSPC);
        // Simulate a drain after the engine's earlier health check. The upload
        // must report underrun instead of refilling and hiding the timing gap.
        h.registers[0x50] = 0;
        const int prior_writes = h.buffer_writes;
        assert(b->dac_upload(words.data(), 2) == EPIPE && h.buffer_writes == prior_writes);
        assert(b->dac_stop() == 0 && h.registers[8] == 0 && h.registers[0x50] == 0);
        require_preserved();
        for (const auto &w : h.writes) assert(w.offset != 0); // Normal stop needs no reset.
        assert(b->dac_upload(words.data(), 2) == EINVAL);
        assert(b->dac_start() == EINVAL);
        prepare(*b, 1, false);
        assert(h.maps == 2 && h.unmaps == 1 && h.allocations == 2);
        auto single = waveform(1, 3);
        assert(b->dac_upload(single.data(), single.size()) == 0);
        assert(b->dac_start() == 0 && !(h.registers[0x40] & (1u << 9)));
    }
    assert(h.unmaps == h.maps && h.closes == 1 && !h.mapped);
    assert(h.registers[8] == 0 && h.registers[0x50] == 0);
    require_preserved();
}

static void check_preloaded_stop_and_bounds() {
    h = Hardware{}; h.registers[0x50] = 77; // Stale preloaded data while already stopped.
    auto b = open_test();
    prepare(*b);
    require_preserved();
    assert(h.registers[0x50] == 0);
    auto words = waveform(2, 4);
    assert(b->dac_upload(nullptr, 2) == EINVAL);
    assert(b->dac_upload(words.data(), 1) == EINVAL);
    assert(b->dac_upload(words.data(), 8194) == ENOSPC); // Must not inspect beyond input.
    const uint32_t invalid[] = {0x00020000, 0x00010000};
    assert(b->dac_upload(invalid, 2) == EINVAL);
    const uint32_t command[] = {0x00a08000, 0x00010000};
    assert(b->dac_upload(command, 2) == EINVAL);
    assert(b->dac_upload(nullptr, 0) == 0 && !h.buffer_writes);
    assert(b->dac_upload(words.data(), words.size()) == 0);
    assert(b->dac_stop() == 0 && h.registers[0x50] == 0);
    require_preserved(); // Preload cancellation uses DAC-only reset, restores other enables.
}

static void check_failures() {
    for (uint32_t mode : {1u << 20, 1u << 27}) {
        h = Hardware{}; h.registers[0x48] |= mode;
        auto b = open_test(); double actual = 0;
        assert(b->dac_prepare(1, 1000, false, actual) == ENOTSUP && h.writes.empty());
    }
    h = Hardware{}; h.registers[8] = 500;
    { auto b = open_test(); double actual = 0;
      assert(b->dac_prepare(1, 1000, false, actual) == EBUSY && h.writes.empty()); }
    h = Hardware{}; h.registers[0x0c] = 50000001;
    { auto b = open_test(); double actual = 0;
      assert(b->dac_prepare(4, 250000, false, actual) == 0);
      assert(actual == 50000001.0 / 201 && actual * 4 <= 1000000); }
    h = Hardware{}; h.registers[0x0c] = 0;
    { auto b = open_test(); double actual = 0;
      assert(b->dac_prepare(1, 1000, false, actual) == EIO && h.writes.empty()); }
    for (bool allocation : {false, true}) {
        h = Hardware{}; h.fail_allocation = allocation; h.fail_map = !allocation;
        auto b = open_test(); double actual = 0;
        assert(b->dac_prepare(1, 1000, false, actual) == ENOMEM && h.writes.empty());
    }
    h = Hardware{};
    { auto b = open_test(); prepare(*b); h.fail_unmap = true; double actual = 0;
      assert(b->dac_prepare(1, 1000, false, actual) == EIO && h.allocations == 1);
      h.fail_unmap = false; }
    h = Hardware{}; h.busy = true;
    { auto b = open_test(); double actual = 0;
      assert(b->dac_prepare(1, 1000, false, actual) == ETIMEDOUT && h.commands.empty());
      h.busy = false; assert(b->dac_stop() == 0); }
    h = Hardware{}; h.bad_channels = true;
    { auto b = open_test(); double actual = 0;
      assert(b->dac_prepare(2, 1000, false, actual) == EIO);
      assert(b->dac_stop() == 0); }
    for (bool wrong_readback : {false, true}) {
        h = Hardware{};
        auto b = open_test(); prepare(*b);
        auto words = waveform(2, 4);
        assert(b->dac_upload(words.data(), words.size()) == 0);
        h.bad_start = wrong_readback;
        if (!wrong_readback) h.fail_write = 8;
        assert(b->dac_start() == EIO);
        assert(h.registers[8] == 0 && h.registers[0x50] == 0);
        require_preserved();
    }
    h = Hardware{};
    { auto b = open_test(); prepare(*b); auto words = waveform(2, 4);
      h.fail_buffer = true; assert(b->dac_upload(words.data(), words.size()) == EFAULT);
      h.fail_buffer = false; assert(b->dac_upload(words.data(), words.size()) == 0);
      assert(b->dac_start() == 0);
      h.fail_read = 0x50; uint32_t q = 1; bool empty = false;
      assert(b->dac_state(q, empty) == ENXIO && q == 0 && empty);
      h.fail_read = -1; h.registers[0x50] = 8193;
      assert(b->dac_state(q, empty) == EIO);
      h.registers[0x50] = 8; h.fail_stop = true;
      assert(b->dac_stop() == EIO); // Do not issue immediate SPI commands into running waveform.
      h.fail_stop = false; assert(b->dac_stop() == 0);
      require_preserved(); }
}


static void check_immediate_ltc() {
    h = Hardware{}; h.id = 0xc05e; // ADC resolution does not determine DAC width.
    auto b = open_test();
    assert(b->model().dac_chip == aioaio::DacChip::ltc2664 && b->model().dac_count == 4);
    assert(b->dac_output_value(0, 0) == ENODATA);
    assert(b->dac_set_range(4, rcBipolar10) == EINVAL);
    assert(b->dac_set_range(ciAll, rcCurrent420) == EINVAL);
    assert(b->dac_set_range(0, rcBipolar12) == EINVAL);
    assert(b->dac_set_range(0, rcRangeCode5) == EINVAL);
    assert(b->dac_set_ranges(1, nullptr) == EINVAL);
    assert(b->dac_set_ranges(0, nullptr) == 0);
    assert(b->dac_set_range(ciAll, rcNoChange) == 0);
    assert(b->dac_output_counts(0, 65536) == ERANGE);
    assert(b->dac_output_counts(0, UINT64_MAX) == ERANGE);
    assert(h.commands.empty());
    assert(b->dac_output_counts(ciAll, 65535) == 0 && h.commands.back() == 0xa0ffff);
    assert(b->dac_output_counts(3, 0x1234) == 0 && h.commands.back() == 0x331234);
    assert(b->dac_output_value(3, 0) == ENODATA); // Counts do not imply a known span.
    assert(b->dac_set_range(ciAll, rcBipolar10) == 0 && h.commands.back() == 0xe00003);
    assert(b->dac_output_value(ciAll, 0) == 0);
    for (size_t i = 0; i < 4; ++i)
        assert(h.commands[h.commands.size() - 4 + i] == (0x308000u | (i << 16)));

    const uint32_t ranges[]{rcUnipolar5, rcUnipolar10, rcBipolar5, rcBipolar10, rcBipolar2_5};
    const double low[]{0, 0, -5, -10, -2.5}, span[]{5, 10, 10, 20, 5};
    for (uint32_t range = 0; range < 5; ++range) {
        assert(b->dac_set_range(1, ranges[range]) == 0);
        assert(h.commands.back() == (0x610000 | range));
        const double lsb = span[range] / 65536;
        assert(b->dac_output_value(1, low[range]) == 0 && h.commands.back() == 0x310000);
        assert(b->dac_output_value(1, low[range] + span[range] - lsb) == 0 && h.commands.back() == 0x31ffff);
        assert(b->dac_output_value(1, low[range] + 0.5 * lsb) == 0 && h.commands.back() == 0x310000);
        assert(b->dac_output_value(1, low[range] + 1.5 * lsb) == 0 && h.commands.back() == 0x310002);
        const size_t before = h.commands.size();
        assert(b->dac_output_value(1, low[range] - lsb / 4) == ERANGE);
        assert(b->dac_output_value(1, low[range] + span[range]) == ERANGE);
        assert(b->dac_output_value(1, low[range] + span[range] - lsb / 4) == ERANGE);
        assert(h.commands.size() == before);
    }
    for (double v : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity()})
        assert(b->dac_output_value(ciAll, v) == EINVAL);
    uint32_t mixed[]{rcUnipolar10, rcNoChange, rcBipolar5};
    assert(b->dac_set_range(ciAll, rcBipolar10) == 0);
    size_t before = h.commands.size();
    assert(b->dac_set_ranges(3, mixed) == 0 && h.commands.size() == before + 3);
    assert(h.commands[before] == 0x600001 && h.commands[before + 1] == 0x620002 && h.commands.back() == 0x630002);
    assert(b->dac_output_value(1, -9) == 0); // NoChange kept +/-10.
    before = h.commands.size();
    assert(b->dac_output_value(ciAll, -1) == ERANGE && h.commands.size() == before);
    mixed[2] = rcCurrent020;
    assert(b->dac_set_ranges(3, mixed) == EINVAL && h.commands.size() == before);
    const uint32_t excess[]{rcRangeCode0, rcRangeCode1, rcRangeCode2, rcRangeCode3};
    assert(b->dac_set_ranges(UINT32_MAX, excess) == 0); // Ignore excess without overread.
}

static uint32_t ad_command(uint32_t command, uint32_t channel, uint32_t data) {
    return ((channel / 4 + 1) << 21) | (command << 18) | ((channel % 4) << 16) | data;
}

static void check_immediate_ad5755() {
    for (uint32_t id : {0xcee8u, 0xce9bu}) {
        h = Hardware{}; h.id = id;
        auto b = open_test();
        const uint32_t channels = id == 0xcee8 ? 8 : 4;
        assert(b->model().dac_chip == aioaio::DacChip::ad5755 && b->model().dac_count == channels);
        assert(b->dac_set_range(channels, rcBipolar10) == EINVAL);
        assert(b->dac_set_range(0, rcBipolar2_5) == EINVAL);
        assert(b->dac_set_range(0, rcRangeCode7) == EINVAL);
        assert(b->dac_set_range(ciAll, rcBipolar10) == 0);
        assert(h.commands.size() == channels * 3);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            assert(h.commands[ch * 3] == ad_command(7, ch, 0x4113));
            assert(h.commands[ch * 3 + 1] == ad_command(0, ch, 0x8000));
            assert(h.commands[ch * 3 + 2] == ad_command(7, ch, 0x4153));
        }
        assert(b->dac_output_counts(ciAll, 0x1234) == 0);
        for (uint32_t ch = 0; ch < channels; ++ch)
            assert(h.commands[h.commands.size() - channels + ch] == ad_command(0, ch, 0x1234));
        const uint32_t ranges[]{rcUnipolar5, rcUnipolar10, rcBipolar5, rcBipolar10,
                                rcCurrent420, rcCurrent020, rcCurrent024};
        const double low[]{0, 0, -5, -10, 4, 0, 0}, span[]{5, 10, 10, 20, 16, 20, 24};
        for (uint32_t range = 0; range < 7; ++range) {
            const uint32_t ch = channels - 1;
            assert(b->dac_set_range(ch, ranges[range]) == 0);
            assert(h.commands[h.commands.size() - 3] == ad_command(7, ch, 0x4110 | range));
            assert(h.commands[h.commands.size() - 2] == ad_command(0, ch, range == 2 || range == 3 ? 0x8000 : 0));
            assert(h.commands.back() == ad_command(7, ch, 0x4150 | range));
            assert(b->dac_output_value(ch, low[range]) == 0 && h.commands.back() == ad_command(0, ch, 0));
            assert(b->dac_output_value(ch, low[range] + span[range] / 2) == 0 && h.commands.back() == ad_command(0, ch, 0x8000));
            assert(b->dac_output_value(ch, low[range] + span[range] * 65535.0 / 65536) == 0 && h.commands.back() == ad_command(0, ch, 0xffff));
            assert(b->dac_output_value(ch, low[range] + span[range]) == ERANGE);
            assert(b->dac_output_value(ch, low[range] - 0.001) == ERANGE);
        }
    }
}

static void check_immediate_errors_and_state() {
    for (uint32_t id : {0xc0e8u, 0xc0ebu, 0xcee8u, 0xce9bu}) {
        h = Hardware{}; h.id = id;
        auto b = open_test();
        h.registers[8] = 100;
        assert(b->dac_set_range(0, rcBipolar10) == (id == 0xc0e8 ? 0 : EBUSY));
        const size_t before = h.commands.size();
        h.registers[8] = 0; h.registers[0x48] |= 1u << 20;
        assert(b->dac_output_counts(0, 0) == ENOTSUP && h.commands.size() == before);
        h.registers[0x48] = 1u << 27;
        assert(b->dac_output_counts(0, 0) == (id == 0xc0eb ? ENOTSUP : 0));
    }
    h = Hardware{}; h.id = 0xc0e8;
    {
        auto b = open_test();
        assert(b->dac_set_range(0, rcBipolar10) == 0);
        const size_t before = h.commands.size();
        assert(b->dac_output_value(ciAll, 0) == ENODATA && h.commands.size() == before);
        assert(b->dac_set_range(ciAll, rcBipolar10) == 0);
        uint32_t mixed[]{rcUnipolar5, rcNoChange, rcBipolar5};
        h.fail_command = h.command_attempts + 2;
        assert(b->dac_set_ranges(3, mixed) == EIO);
        assert(b->dac_output_value(0, -1) == ERANGE); // Successful first range committed.
        assert(b->dac_output_value(1, -9) == 0); // NoChange untouched.
        assert(b->dac_output_value(2, 0) == ENODATA); // Failed write may have reached hardware.
        assert(b->dac_output_value(3, -9) == 0); // Later channel untouched.
        h.fail_command = h.command_attempts + 1;
        assert(b->dac_set_range(ciAll, rcBipolar5) == EIO);
        for (uint32_t ch = 0; ch < 4; ++ch) assert(b->dac_output_value(ch, 0) == ENODATA);
        h.fail_read = 4;
        assert(b->dac_set_range(0, rcBipolar10) == ENXIO);
        h.fail_read = -1; h.busy = true;
        assert(b->dac_output_counts(0, 0) == ETIMEDOUT);
        h.busy = false;
        assert(b->dac_set_range(0, rcBipolar10) == 0);
        h.fail_status_after_command = h.command_attempts + 1;
        assert(b->dac_set_range(0, rcUnipolar5) == ENXIO);
        assert(h.commands.back() == 0x600000); // Command reached hardware before failed busy read.
        assert(b->dac_output_value(0, 0) == ENODATA);
        h.fail_status_after_command = -1;
        assert(b->dac_set_range(0, rcBipolar10) == 0);
    }
    // Each stage of the AD5755 sequence must propagate failure, leave the
    // failed channel's range unknown, and stop before touching later channels.
    for (int stage = 1; stage <= 3; ++stage) {
        h = Hardware{}; h.id = 0xcee8;
        auto b = open_test();
        assert(b->dac_set_range(ciAll, rcBipolar10) == 0);
        h.fail_command = h.command_attempts + stage;
        assert(b->dac_set_range(ciAll, rcCurrent020) == EIO);
        assert(b->dac_output_value(0, 0) == ENODATA);
        assert(b->dac_output_value(1, -9) == 0);
        h.fail_command = h.command_attempts + 2;
        const size_t before = h.commands.size();
        assert(b->dac_output_counts(ciAll, 0x3456) == EIO && h.commands.size() == before + 1);
    }
    // A failed untouched waveform start preserves immediate state. Successful
    // waveform setup/cleanup owns all ranges and establishes +/-10 V.
    h = Hardware{};
    {
        auto b = open_test();
        assert(b->dac_set_range(ciAll, rcUnipolar5) == 0);
        double actual = 0;
        assert(b->dac_prepare(0, 1, false, actual) == EINVAL);
        assert(b->dac_output_value(0, 2.5) == 0 && h.commands.back() == 0x308000);
        prepare(*b);
        const size_t before = h.commands.size();
        assert(b->dac_set_range(0, rcUnipolar5) == EBUSY);
        assert(b->dac_output_counts(0, 0) == EBUSY);
        assert(b->dac_output_value(0, 0) == EBUSY && h.commands.size() == before);
        assert(b->dac_stop() == 0);
        assert(b->dac_output_value(0, 0) == 0 && h.commands.back() == 0x308000);
        assert(b->dac_set_range(0, rcUnipolar5) == 0);
        assert(b->dac_output_value(0, 0) == 0 && h.commands.back() == 0x300000);
    }
}

static void check_immediate_public_api() {
    h = Hardware{};
    TBoardIndex board = biNone;
    assert(DAC_SetRange1(biNone, 0, rcBipolar10) == ENODEV);
    assert(DAC_SetRangeAll(biNone, 0, nullptr) == ENODEV);
    assert(DAC_OutputCounts(biNone, 0, 0) == ENODEV);
    assert(DAC_OutputV(biNone, 0, 0) == ENODEV);
    assert(BRD_Open("/dev/apci/dac-test", &board) == 0);
    TBoardCapabilities caps{}; caps.StructSize = sizeof(caps);
    assert(BRD_GetCapabilities(board, &caps) == 0 && caps.DACCount == 4 && caps.DACResolutionBits == 16);
    assert(DAC_OutputV(board, 0, 0) == ENODATA);
    assert(DAC_SetRange1(board, ciAll, rcBipolar10) == 0);
    assert(DAC_OutputV(board, 0, 0) == 0 && h.commands.back() == 0x308000);
    assert(DAC_OutputCounts(board, 2, 65535) == 0 && h.commands.back() == 0x32ffff);
    uint32_t ranges[]{rcUnipolar5, rcNoChange};
    assert(DAC_SetRangeAll(board, 2, ranges) == 0);
    assert(DAC_OutputV(board, 0, 0) == 0 && h.commands.back() == 0x300000);
    const size_t before = h.commands.size();
    assert(BRD_Close(board) == 0 && h.commands.size() == before); // Immediate output persists.
    assert(DAC_OutputV(board, 0, 0) == ENODEV);
    for (uint32_t id : {0x80e8u, 0x805eu, 0x82ecu}) {
        h = Hardware{}; h.id = id;
        assert(BRD_Open("/dev/apci/dac-test", &board) == 0);
        caps.StructSize = sizeof(caps);
        assert(BRD_GetCapabilities(board, &caps) == 0 && caps.DACCount == 0 && caps.DACResolutionBits == 0);
        assert(DAC_SetRange1(board, ciAll, rcBipolar10) == ENOTSUP);
        assert(DAC_SetRangeAll(board, 1, ranges) == ENOTSUP);
        assert(DAC_OutputCounts(board, 0, 0) == ENOTSUP);
        assert(DAC_OutputV(board, 0, 0) == ENOTSUP);
        assert(BRD_Close(board) == 0 && h.writes.empty());
    }
    assert(AIO_Shutdown() == 0);
}

int main() {
    check_immediate_ltc();
    check_immediate_ad5755();
    check_immediate_errors_and_state();
    check_immediate_public_api();
    check_capabilities_and_validation();
    check_playback_and_cleanup();
    check_preloaded_stop_and_bounds();
    check_failures();
    std::puts("DAC immediate LTC2664/AD5755, conversion, errors, range state, waveform, and cleanup checks passed; no hardware accessed.");
}
