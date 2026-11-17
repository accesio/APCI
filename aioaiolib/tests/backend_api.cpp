#include "backend.hpp"
#include "AIOAIO.h"
#include "apcilib.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <vector>

namespace {
struct Access {
    unsigned width;
    int bar, offset;
    uint32_t value;
};

struct Hardware {
    unsigned id = 0xc0e8, allocations = 0, maps = 0, unmaps = 0;
    std::map<int, uint32_t> regs{
        {0, 0}, {0x0c, 50000000}, {0x10, 5000}, {0x14, 123},
        {0x18, 0x12345670}, {0x1c, 0x76543210}, {0x20, 444},
        {0x38, 0x8fcf}, {0x3c, 0x8bcf}, {0x40, 0x0a000280}, {0x48, 2}
    };
    std::vector<Access> reads, writes;
    std::deque<uint32_t> fifo;
    std::vector<uint32_t> dma;
    int fail_read = -1, fail_write = -1;
    bool no_samples = false, invalid = false, wrong_tag = false;
    bool stuck_reset = false, fail_reset = false;
} h;

std::unique_ptr<aioaio::Backend> open_test() {
    std::unique_ptr<aioaio::Backend> backend;
    assert(aioaio::open_backend("/dev/apci/api-test", backend) == 0);
    return backend;
}

int reading(unsigned width, int bar, int offset, uint32_t &value) {
    h.reads.push_back({width, bar, offset, 0});
    if (int(h.reads.size()) == h.fail_read) {
        errno = ENXIO;
        return -1;
    }
    if (bar == 0) {
        assert(offset == 0x1c);
        value = 0;
        return 0;
    }
    if (offset == 0x28) {
        value = h.fifo.size();
    } else if (offset == 0x30) {
        value = h.fifo.empty() ? 0x80000000 : h.fifo.front();
        if (!h.fifo.empty()) h.fifo.pop_front();
    } else {
        value = h.regs[offset];
    }
    return 0;
}

int writing(unsigned width, int bar, int offset, uint32_t value) {
    h.writes.push_back({width, bar, offset, value});
    if (int(h.writes.size()) == h.fail_write ||
        (h.fail_reset && bar == 1 && offset == 0 && value == 1)) {
        errno = EIO;
        return -1;
    }
    if (bar == 0) {
        assert(offset == 0x1c && value == 8);
        return 0;
    }
    if (width != 4) {
        h.regs[offset] = value;
        return 0;
    }
    if (offset == 0x40) {
        h.regs[offset] = (value & 0xffff) | ((h.regs[offset] & 0xffff0000) & ~value);
    } else if (offset == 0) {
        if (value & 1) {
            h.regs[0] = h.stuck_reset ? 1 : 0;
            h.regs[0x10] = h.regs[0x14] = h.regs[0x20] = h.regs[0x40] = h.regs[0x48] = 0;
            h.regs[0x38] = h.regs[0x3c] = 0x8fcf;
            h.fifo.clear();
        } else {
            h.regs[0] = value & ~0x40u;
            if (value & 0x40) {
                h.fifo.clear();
                h.regs[0x20] = 0;
            }
        }
    } else if (offset == 0x38 || offset == 0x3c) {
        h.regs[offset] = value & ~(1u << 17);
        if ((value & (1u << 16)) && h.regs[0x10] == 0) {
            const unsigned adc = offset == 0x3c;
            const unsigned channel = (value >> 12) & 7;
            const unsigned gain = (value >> 7) & 7;
            uint32_t word = (1u << 24) | (adc << 23) | (channel << 20) |
                ((value & (1u << 11)) ? 0 : (1u << 19)) | (gain << 16) |
                (adc * 100 + channel);
            if (h.wrong_tag) word ^= 1u << 20;
            if (h.invalid) word |= 0x80000000;
            if (!h.no_samples) h.fifo.push_back(word);
            h.regs[offset] &= ~(1u << 16);
        }
    } else {
        h.regs[offset] = value;
    }
    return 0;
}
} // namespace

extern "C" {
int __wrap_open(const char *path, int, ...) {
    assert(std::string(path) == "/dev/apci/api-test");
    return 123;
}
int __wrap_fstat(int fd, struct stat *status) {
    assert(fd == 123);
    *status = {};
    status->st_mode = S_IFCHR;
    return 0;
}
int __wrap_flock(int fd, int) {
    assert(fd == 123);
    return 0;
}
int __wrap_close(int fd) {
    assert(fd == 123);
    return 0;
}
void *__wrap_mmap(void *, size_t size, int prot, int flags, int fd, off_t offset) {
    assert(fd == 123 && offset == 0 && prot == PROT_READ && flags == MAP_SHARED &&
           size == h.dma.size() * 4);
    ++h.maps;
    return h.dma.data();
}
int __wrap_munmap(void *address, size_t size) {
    assert(address == h.dma.data() && size == h.dma.size() * 4);
    ++h.unmaps;
    return 0;
}
int apci_get_device_info(int fd, unsigned long index, unsigned int *id, unsigned long *) {
    assert(fd == 123 && index == 0);
    *id = h.id;
    return 0;
}
int apci_read8(int, unsigned long, int bar, int offset, __u8 *value) {
    uint32_t result = 0;
    const int status = reading(1, bar, offset, result);
    *value = result;
    return status;
}
int apci_read16(int, unsigned long, int bar, int offset, __u16 *value) {
    uint32_t result = 0;
    const int status = reading(2, bar, offset, result);
    *value = result;
    return status;
}
int apci_read32(int, unsigned long, int bar, int offset, __u32 *value) {
    return reading(4, bar, offset, *value);
}
int apci_write8(int, unsigned long, int bar, int offset, __u8 value) {
    return writing(1, bar, offset, value);
}
int apci_write16(int, unsigned long, int bar, int offset, __u16 value) {
    return writing(2, bar, offset, value);
}
int apci_write32(int, unsigned long, int bar, int offset, __u32 value) {
    return writing(4, bar, offset, value);
}
int apci_dma_transfer_size(int, unsigned long, __u8 count, size_t bytes) {
    ++h.allocations;
    h.dma.assign(count * bytes / 4, 0);
    return 0;
}
int apci_dma_data_ready(int, unsigned long, int *, int *, int *) {
    assert(false);
    return -1;
}
int apci_dma_data_done(int, unsigned long, int) {
    assert(false);
    return -1;
}
int apci_wait_for_irq(int, unsigned long) {
    assert(false);
    return -1;
}
int apci_cancel_irq(int, unsigned long) {
    assert(false);
    return -1;
}
int apci_dac_buffer_size(int, unsigned long) {
    assert(false);
    return -1;
}
int apci_writebuf32(int, unsigned long, int, int, unsigned int, int) {
    assert(false);
    return -1;
}
} // extern "C"

static void registers() {
    h = Hardware{};
    auto backend = open_test();
    uint32_t value = 0;
    for (unsigned width : {1u, 2u, 4u}) {
        const unsigned offset = 4096 - width;
        assert(backend->write_register(width, offset, 0xa5) == 0);
        assert(h.writes.back().width == width && h.writes.back().bar == 1 &&
               h.writes.back().offset == int(offset));
        assert(backend->read_register(width, offset, value) == 0 && value == 0xa5);
        assert(backend->read_register(width, 4096, value) == EINVAL);
        assert(backend->write_register(width, UINT32_MAX, 0) == EINVAL);
    }
    assert(backend->read_register(3, 0, value) == EINVAL);
    assert(backend->read_register(4, 2, value) == EINVAL);
    assert(backend->write_register(2, 1, 0) == EINVAL);

    h.fail_read = h.reads.size() + 1;
    value = 123;
    assert(backend->read_register(4, 0, value) == ENXIO && value == 0);
    h.fail_write = h.writes.size() + 1;
    assert(backend->write_register(4, 0x44, 123) == EIO);
    assert(backend->write_register(4, 0x40, 1) == EBUSY);
    assert(backend->write_register(1, 0x40, 4) == EBUSY);
    assert(backend->write_register(4, 0x40, 0x280) == 0);

    uint8_t bytes[]{11, 22, 33};
    uint16_t words[]{0x1234, 0x5678};
    uint32_t dwords[]{0x12345678, 0xabcdef01}, got[3]{};
    size_t before = h.writes.size();
    assert(backend->write_register_block(1, 0x44, 3, bytes) == 0);
    assert(backend->write_register_block(2, 0x44, 2, words) == 0);
    assert(backend->write_register_block(4, 0x44, 2, dwords) == 0);
    for (size_t i = before; i < h.writes.size(); ++i) assert(h.writes[i].offset == 0x44);
    assert(h.writes[before].value == 11 && h.writes[before + 3].value == 0x1234 &&
           h.writes.back().value == 0xabcdef01);

    h.regs[0x44] = 0x76543210;
    assert(backend->read_register_block(4, 0x44, 3, got) == 0);
    for (auto result : got) assert(result == 0x76543210);
    assert(backend->read_register_block(1, 0x44, 0, nullptr) == 0);
    assert(backend->write_register_block(4, 0x44, 0, nullptr) == 0);
    assert(backend->read_register_block(1, 0x44, 1, nullptr) == EINVAL);
    assert(backend->write_register_block(4, 0x44, UINT32_MAX, dwords) == EOVERFLOW);

    got[0] = got[1] = got[2] = 0xfeed;
    h.fail_read = h.reads.size() + 2;
    assert(backend->read_register_block(4, 0x44, 3, got) == ENXIO);
    assert(got[0] == 0x76543210 && got[1] == 0xfeed && got[2] == 0xfeed);
    before = h.writes.size();
    h.fail_write = before + 2;
    assert(backend->write_register_block(1, 0x44, 3, bytes) == EIO &&
           h.writes.size() == before + 2);
}

static void immediate() {
    for (bool differential : {false, true}) {
        for (unsigned id : {0xc0e8u, 0x805eu}) {
            h = Hardware{};
            h.id = id;
            auto backend = open_test();
            aioaio::Config config;
            config.range_codes.fill(7);
            config.differential = differential;
            const unsigned adcs = backend->model().adc_count;
            const unsigned per_adc = differential ? 4 : 8;
            for (unsigned adc = 0; adc < adcs; ++adc)
                for (unsigned channel = 0; channel < per_adc; ++channel)
                    config.range_codes[adc * 8 + (differential ? 2 * channel : channel)] = channel % 6;

            const auto previous = h.regs;
            uint32_t words[16]{}, count = 99;
            assert(backend->immediate_scan(config, words, count) == 0 && count == adcs * per_adc);
            for (unsigned adc = 0; adc < adcs; ++adc) {
                for (unsigned channel = 0; channel < per_adc; ++channel) {
                    const unsigned physical = differential ? 2 * channel : channel;
                    const auto word = words[adc * per_adc + channel];
                    assert((word & 0xffff) == adc * 100 + physical &&
                           ((word >> 20) & 7) == physical &&
                           ((word >> 16) & 7) == channel % 6 &&
                           ((word >> 19) & 1) == unsigned(differential));
                }
            }
            for (int offset : {0x10, 0x14, 0x18, 0x1c, 0x20, 0x38, 0x3c, 0x48})
                assert(h.regs[offset] == previous.at(offset));
            assert((h.regs[0x40] & 0xffff) == (previous.at(0x40) & 0xffff));
            for (auto &access : h.writes) assert(access.offset != 0x18 && access.offset != 0x1c);
            assert(h.fifo.empty() && h.allocations == 0);
        }
    }

    h = Hardware{};
    auto backend = open_test();
    aioaio::Config config;
    config.range_codes.fill(7);
    uint32_t output[16], count = 0;
    for (auto &value : output) value = 0xfeed;
    h.regs[0x48] |= 1u << 18;
    assert(backend->immediate_scan(config, output, count) == ENOTSUP && h.writes.empty());
    h.regs[0x48] = 2;
    h.regs[0x3c] |= 1u << 16;
    assert(backend->immediate_scan(config, output, count) == EBUSY && h.writes.empty());
    h.regs[0x3c] &= ~(1u << 16);
    config.range_codes[15] = 6;
    assert(backend->immediate_scan(config, output, count) == EINVAL && h.writes.empty());
    config.range_codes.fill(7);

    for (int failure = 0; failure < 3; ++failure) {
        h.no_samples = failure == 0;
        h.invalid = failure == 1;
        h.wrong_tag = failure == 2;
        assert(backend->immediate_scan(config, output, count) ==
               (failure == 0 ? ETIMEDOUT : EIO) && count == 0);
        for (auto value : output) assert(value == 0xfeed);
        assert(h.regs[0x10] == 5000 && h.regs[0x20] == 444 &&
               !(h.regs[0x38] & (1u << 16)));
    }
    h.no_samples = h.invalid = h.wrong_tag = false;
    double hz = 0;
    assert(backend->configure(config, hz) == 0);
    h.fail_write = h.writes.size() + 3;
    assert(backend->immediate_scan(config, output, count) == EIO && count == 0);
    assert(backend->prepare_dma(4, 4) == EINVAL);
}

static void reset() {
    h = Hardware{};
    auto backend = open_test();
    aioaio::Config config;
    config.range_codes.fill(7);
    double hz = 0;
    assert(backend->configure(config, hz) == 0 && backend->dac_set_range(ciAll, rcBipolar10) == 0);
    assert(backend->write_register(4, 0x44, 0x123) == 0);
    assert(backend->dac_output_value(0, 0) == ENODATA && backend->prepare_dma(4, 4) == EINVAL);

    assert(backend->configure(config, hz) == 0 && backend->prepare_dma(4, 4) == 0 &&
           backend->start_adc() == 0);
    uint32_t value, count, words[16];
    assert(backend->read_register(4, 0, value) == EBUSY &&
           backend->write_register(4, 0x44, 0) == EBUSY &&
           backend->immediate_scan(config, words, count) == EBUSY &&
           backend->reset_board() == EBUSY);
    assert(backend->stop_adc() == 0 && backend->reset_board() == 0);
    assert(h.unmaps == 1 && h.regs[0x10] == 0 && h.regs[0x40] == 0);
    assert(backend->prepare_dma(4, 4) == EINVAL && backend->dac_output_value(0, 0) == ENODATA);

    h.stuck_reset = true;
    assert(backend->reset_board() == ETIMEDOUT);
    assert(backend->write_register(4, 0x44, 0) == EIO);
    assert(backend->configure(config, hz) == EIO);
    h.stuck_reset = false;
    h.fail_reset = true;
    assert(backend->reset_board() == EIO);
    h.fail_reset = false;
    assert(backend->reset_board() == 0);

    assert(backend->configure(config, hz) == 0 && backend->prepare_dma(4, 4) == 0 &&
           backend->start_adc() == 0 && backend->stop_adc() == 0);
    assert(backend->write_register(4, 0x44, 0xabc) == 0);
    const size_t before = h.writes.size();
    backend.reset();
    assert(h.writes.size() == before);
}

int main() {
    registers();
    immediate();
    reset();
    std::puts("Backend API: native immediate ADC, restoration/errors, reset, register/block I/O "
              "and cache ownership passed; no hardware accessed.");
}
