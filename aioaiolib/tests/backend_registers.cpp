#include "backend.hpp"
#include "apcilib.h"

#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" int __real_close(int);

// Every device-facing operation is a link-time stub. This executable never
// opens a real node, allocates real DMA, or accesses hardware registers.
namespace {
struct Write { int bar, offset; uint32_t value; };
struct Hardware {
    unsigned id = 0xc2ef;
    bool character_device = true;
    bool fail_map = false;
    bool wrong_threshold = false;
    bool wrong_gain = false, wrong_configuration = false, reject_go = false;
    bool fail_firmware_read = false;
    int opens = 0, closes = 0, maps = 0, unmaps = 0, allocations = 0;
    int first = 0, ready = 0, discarded = 0, acknowledged = 0;
    int wait_error = 0, cancel_error = 0;
    uint32_t ring_slots = 0;
    size_t slot_bytes = 0;
    std::map<std::pair<int, int>, uint32_t> registers;
    std::map<std::pair<int, int>, int> reads;
    std::vector<Write> writes;
    std::vector<uint32_t> dma;
    Hardware() {
        registers[{1, 0x0c}] = 50000000;
        registers[{1, 0}] = 0x400; // DAAI HART reset level must survive FIFO clear.
        registers[{1, 0x40}] = 0x01800280; // unrelated LDAC/DIO status + enables
        registers[{1, 0x68}] = 0x00050001; // Raw firmware value, without assuming its encoding.
    }
} h;

class StderrCapture {
public:
    StderrCapture() {
        std::fflush(stderr);
        saved_ = ::dup(STDERR_FILENO);
        file_ = std::tmpfile();
        assert(saved_ >= 0 && file_);
        assert(::dup2(::fileno(file_), STDERR_FILENO) >= 0);
    }
    ~StderrCapture() {
        restore();
        std::fclose(file_);
    }
    std::string finish() {
        restore();
        std::rewind(file_);
        std::string result;
        char buffer[1024];
        while (const size_t count = std::fread(buffer, 1, sizeof(buffer), file_))
            result.append(buffer, count);
        assert(!std::ferror(file_));
        return result;
    }
private:
    void restore() {
        if (saved_ < 0) return;
        std::fflush(stderr);
        assert(::dup2(saved_, STDERR_FILENO) >= 0);
        // close() is wrapped for the device fixture. This is an ordinary fd.
        assert(__real_close(saved_) == 0);
        saved_ = -1;
    }
    FILE *file_ = nullptr;
    int saved_ = -1;
};

std::unique_ptr<aioaio::Backend> open_test() {
    std::unique_ptr<aioaio::Backend> backend;
    assert(aioaio::open_backend("/dev/apci/test-fixture", backend) == 0);
    assert(backend);
    return backend;
}

aioaio::Config configuration() {
    aioaio::Config c;
    c.range_codes.fill(7);
    return c;
}

size_t write_position(int bar, int offset, uint32_t bits) {
    for (size_t i = 0; i < h.writes.size(); ++i)
        if (h.writes[i].bar == bar && h.writes[i].offset == offset &&
            (h.writes[i].value & bits) == bits) return i;
    return h.writes.size();
}
}

extern "C" {
int __wrap_open(const char *path, int, ...) {
    assert(std::string(path) == "/dev/apci/test-fixture");
    ++h.opens;
    return 123;
}
int __wrap_fstat(int fd, struct stat *st) {
    assert(fd == 123);
    *st = {};
    st->st_mode = h.character_device ? S_IFCHR : S_IFREG;
    return 0;
}
int __wrap_flock(int fd, int) { assert(fd == 123); return 0; }
int __wrap_close(int fd) { assert(fd == 123); ++h.closes; return 0; }
void *__wrap_mmap(void *, size_t length, int protection, int flags, int fd, off_t offset) {
    assert(fd == 123 && offset == 0 && protection == PROT_READ && flags == MAP_SHARED);
    assert(length == h.slot_bytes * h.ring_slots);
    if (h.fail_map) { errno = ENOMEM; return MAP_FAILED; }
    ++h.maps;
    return h.dma.data();
}
int __wrap_munmap(void *address, size_t length) {
    assert(address == h.dma.data());
    assert(length == h.slot_bytes * h.ring_slots);
    ++h.unmaps;
    return 0;
}
int apci_get_device_info(int fd, unsigned long index, unsigned int *id, unsigned long *) {
    assert(fd == 123 && index == 0);
    *id = h.id;
    return 0;
}
int apci_read8(int, unsigned long, int, int, __u8 *) { assert(false); return -1; }
int apci_read16(int, unsigned long, int, int, __u16 *) { assert(false); return -1; }
int apci_write16(int, unsigned long, int, int, __u16) { assert(false); return -1; }
int apci_read32(int fd, unsigned long index, int bar, int offset, __u32 *data) {
    assert(fd == 123 && index == 0);
    ++h.reads[{bar, offset}];
    if (h.fail_firmware_read && bar == 1 && offset == 0x68) {
        errno = ENOTSUP;
        return -1;
    }
    *data = h.registers[{bar, offset}];
    if (h.wrong_threshold && bar == 1 && offset == 0x20) ++*data;
    if (h.wrong_gain && bar == 1 && offset == 0x18) *data = 0;
    return 0;
}
int apci_write32(int fd, unsigned long index, int bar, int offset, __u32 data) {
    assert(fd == 123 && index == 0 && bar == 1);
    h.writes.push_back({bar, offset, data});
    if (offset == 0x40) {
        const uint32_t statuses = h.registers[{bar, offset}] & 0xffff0000u;
        h.registers[{bar, offset}] = (data & 0xffffu) | (statuses & ~data);
    } else if (offset == 0) {
        // Only the ADC FIFO reset pulse may change the preexisting level bits.
        assert(data == 0x400 || data == 0x440);
        h.registers[{bar, offset}] = data;
        if (data & 0x40) {
            h.registers[{bar, 0x28}] = 0;
            h.registers[{bar, 0x20}] = 0;
        }
    } else {
        h.registers[{bar, offset}] = data;
        if (offset == 0x38) {
            h.registers[{bar, offset}] &= ~(1u << 17); // config-load strobe
            if (h.wrong_configuration && (data & (1u << 17)))
                h.registers[{bar, offset}] ^= 1u << 11;
            if (h.reject_go) h.registers[{bar, offset}] &= ~(1u << 16);
            const auto *m = aioaio::find_model(h.id);
            if (m && m->adc_count == 2)
                h.registers[{bar, 0x3c}] = (h.registers[{bar, 0x3c}] & 0xffffu) |
                                         (h.registers[{bar, offset}] & 0x70000u);
        }
    }
    return 0;
}
int apci_write8(int fd, unsigned long index, int bar, int offset, __u8 data) {
    assert(fd == 123 && index == 0 && bar == 0 && offset == 0x1c && data == 8);
    h.writes.push_back({bar, offset, data});
    return 0;
}
int apci_dma_transfer_size(int fd, unsigned long index, __u8 slots, size_t slot_size) {
    assert(fd == 123 && index == 0);
    assert(!(h.registers[{1, 0x38}] & (1u << 16)));
    assert(!(h.registers[{1, 0x40}] & 0x3fu));
    assert(write_position(0, 0x1c, 8) != h.writes.size());
    h.ring_slots = slots;
    h.slot_bytes = slot_size;
    h.dma.assign(slots * slot_size / 4, 0);
    h.first = h.ready = h.discarded = 0;
    ++h.allocations;
    return 0;
}
int apci_dma_data_ready(int fd, unsigned long index, int *first, int *slots, int *discarded) {
    assert(fd == 123 && index == 0);
    *first = h.first; *slots = h.ready; *discarded = h.discarded;
    return 0;
}
int apci_dma_data_done(int fd, unsigned long index, int slots) {
    assert(fd == 123 && index == 0);
    h.acknowledged += slots;
    return 0;
}
int apci_wait_for_irq(int fd, unsigned long index) {
    assert(fd == 123 && index == 0);
    if (h.wait_error) { errno = h.wait_error; return -1; }
    return 0;
}
int apci_dac_buffer_size(int, unsigned long) { assert(false); errno = ENOTSUP; return -1; }
int apci_writebuf32(int, unsigned long, int, int, unsigned int, int) {
    assert(false); errno = ENOTSUP; return -1;
}
int apci_cancel_irq(int fd, unsigned long index) {
    assert(fd == 123 && index == 0);
    if (h.cancel_error) { errno = h.cancel_error; return -1; }
    return 0;
}
}

static void check_rate_limits() {
    assert(::unsetenv("AIOAIO_DEBUG") == 0);
    h = Hardware{};
    h.id = 0x805e;
    {
        auto b = open_test();
        auto c = configuration();
        double rate = 0;
        for (double request : {100000.001, 250000.0, 1000000.0}) {
            c.rate_hz = request;
            assert(b->configure(c, rate) == ERANGE);
            assert(h.reads.empty() && h.writes.empty() && h.allocations == 0);
        }
        c.rate_hz = 100000;
        assert(b->configure(c, rate) == 0 && rate == 100000);
        assert(b->prepare_dma(4, 1024) == 0);
        assert(b->start_adc() == 0);
        assert(h.registers.at({1, 0x10}) == 500);
        assert(b->stop_adc() == 0);
    }
    // Quantization may never round a supported request above the model limit.
    h = Hardware{};
    h.id = 0x805e;
    h.registers[{1, 0x0c}] = 50000001;
    {
        auto b = open_test();
        auto c = configuration();
        c.rate_hz = 100000;
        double rate = 0;
        assert(b->configure(c, rate) == 0 && rate <= 100000);
        assert(rate == 50000001.0 / 501);
        assert(b->prepare_dma(4, 1024) == 0);
        assert(b->start_adc() == 0);
        assert(h.registers.at({1, 0x10}) == 501);
        assert(b->stop_adc() == 0);
    }
}

static void check_startup_diagnostics() {
    // The production mPCIe-ADI12-8E follows the single-ADC path. Debugging
    // must neither change its error result nor make a firmware probe required.
    assert(::unsetenv("AIOAIO_DEBUG") == 0);
    h = Hardware{};
    h.id = 0x805e;
    {
        StderrCapture capture;
        {
            auto b = open_test();
            double rate = 0;
            assert(b->configure(configuration(), rate) == 0);
            assert(b->prepare_dma(4, 1024) == 0);
            assert(b->start_adc() == 0);
            assert(b->stop_adc() == 0);
        }
        assert(h.reads[std::make_pair(1, 0x68)] == 0);
        assert(capture.finish().empty());
    }

    assert(::setenv("AIOAIO_DEBUG", "1", 1) == 0);
    h = Hardware{};
    h.id = 0x805e;
    h.wrong_gain = true;
    {
        StderrCapture capture;
        {
            auto b = open_test();
            double rate = 0;
            assert(b->configure(configuration(), rate) == 0);
            assert(b->configure(configuration(), rate) == 0);
            assert(h.reads[std::make_pair(1, 0x68)] == 1);
            assert(b->prepare_dma(4, 1024) == 0);
            assert(b->start_adc() == EIO);
            assert(!(h.registers.at({1, 0x38}) & (1u << 16)));
            assert(b->stop_adc() == 0);
        }
        const std::string log = capture.finish();
        assert(log.find("0x805e") != std::string::npos);
        assert(log.find("0x68") != std::string::npos);
        assert(log.find("0x00050001") != std::string::npos);
        assert(log.find("FAILED ADC gain at logical BAR[1]+0x18: expected 0x77777777, got 0x00000000, mask 0xffffffff") != std::string::npos);
    }

    h = Hardware{};
    h.id = 0x805e;
    h.fail_firmware_read = true;
    {
        StderrCapture capture;
        {
            auto b = open_test();
            double rate = 0;
            assert(b->configure(configuration(), rate) == 0 && rate == 10000);
            assert(b->configure(configuration(), rate) == 0);
            assert(h.reads[std::make_pair(1, 0x68)] == 1);
            assert(b->prepare_dma(4, 1024) == 0);
            assert(b->start_adc() == 0);
            assert(b->stop_adc() == 0);
        }
        const std::string log = capture.finish();
        assert(log.find("0x68") != std::string::npos);
        assert(log.find(std::to_string(ENOTSUP)) != std::string::npos);
    }

    for (bool reject_go : {false, true}) {
        h = Hardware{};
        h.id = 0x805e;
        h.wrong_configuration = !reject_go;
        h.reject_go = reject_go;
        StderrCapture capture;
        {
            auto b = open_test();
            double rate = 0;
            assert(b->configure(configuration(), rate) == 0);
            assert(b->prepare_dma(4, 1024) == 0);
            assert(b->start_adc() == EIO);
            assert(!(h.registers.at({1, 0x38}) & (1u << 16)));
            assert(b->stop_adc() == 0);
        }
        const std::string log = capture.finish();
        const char *label = reject_go ? "FAILED ADC GO" : "FAILED ADC configuration";
        assert(log.find(label) != std::string::npos);
        assert(log.find("logical BAR[1]+0x38") != std::string::npos);
        assert(log.find("expected 0x") != std::string::npos);
        assert(log.find(", got 0x") != std::string::npos);
        assert(log.find(", mask 0x") != std::string::npos);
    }
    assert(::unsetenv("AIOAIO_DEBUG") == 0);
}

int main() {
    assert(::unsetenv("AIOAIO_DEBUG") == 0);
    {
        auto b = open_test();
        auto c = configuration();
        double rate = 0;
        assert(b->configure(c, rate) == 0 && rate == 10000);
        assert(h.writes.empty()); // Configure stages settings; no hardware start.
        assert(b->prepare_dma(4, 1024) == 0);
        assert(h.slot_bytes == 8192 && h.ring_slots == 4);
        h.writes.clear();
        assert(b->start_adc() == 0);
        assert(h.registers.at({1, 0x10}) == 5000);
        assert(h.registers.at({1, 0x18}) == 0x77777777);
        assert(h.registers.at({1, 0x1c}) == 0x77777777);
        const auto command = h.registers.at({1, 0x38});
        assert(command & (1u << 16));
        assert(!(command & (1u << 18))); // FPGA per-conversion timed mode
        assert((command & 0x30) == 0x20); // ADAS advanced sequencer
        assert(command & 0x800); // single-ended
        assert(((command >> 12) & 7) == 7);
        assert(write_position(1, 0x3c, 0x8000) < write_position(1, 0x38, 0x8000));
        assert((h.registers.at({1, 0x40}) & 0xffffu) == 0x285);
        assert((h.registers.at({1, 0x40}) & 0xffff0000u) == 0x01800000u);
        h.first = 3; h.ready = 2;
        uint32_t first, slots, discarded;
        assert(b->ready(first, slots, discarded) == 0 && first == 3 && slots == 2);
        assert(b->slot(3) == h.dma.data() + 6144);
        assert(!b->slot(4));
        assert(b->acknowledge(3) == EINVAL);
        assert(b->acknowledge(2) == 0 && h.acknowledged == 2);
        assert(b->acknowledge(1) == EINVAL);
        h.ready = 4;
        assert(b->ready(first, slots, discarded) == EIO);
        h.wait_error = EINTR;
        h.cancel_error = EALREADY;
        assert(b->wait_irq() == EINTR && b->cancel_irq() == EALREADY);
        assert(b->stop_adc() == 0);
        assert(!(h.registers.at({1, 0x38}) & (1u << 16)));
        assert((h.registers.at({1, 0x40}) & 0xffffu) == 0x280);
        assert(h.registers.at({1, 0}) == 0x400);
        assert(h.unmaps == 0); // stop retains mapping; resize after worker joins
        assert(b->prepare_dma(4, 512) == 0);
        assert(h.unmaps == 1 && h.maps == 2 && h.allocations == 2);
    }
    assert(h.opens == 1 && h.closes == 1 && h.unmaps == 2);
    h = Hardware{};
    h.id = 0xc0ec;
    {
        auto b = open_test();
        auto c = configuration();
        c.differential = true;
        c.channels_per_adc = 4;
        c.range_codes[0] = 1; c.range_codes[2] = 2;
        c.range_codes[4] = 3; c.range_codes[6] = 4;
        double rate = 0;
        assert(b->configure(c, rate) == 0);
        assert(b->prepare_dma(4, 2048) == EINVAL);
        h.registers[{1, 0x48}] = 1u << 18;
        assert(b->prepare_dma(4, 1024) == ENOTSUP);
        assert(h.writes.empty());
        h.registers[{1, 0x48}] = 0;
        assert(b->prepare_dma(4, 1024) == 0);
        assert(b->start_adc() == 0);
        assert(h.registers.at({1, 0x18}) == 0x04030201);
        assert(!(h.registers.at({1, 0x38}) & 0x800));
        assert(((h.registers.at({1, 0x38}) >> 12) & 7) == 6);
        assert(write_position(1, 0x3c, 0) == h.writes.size());
        assert(b->stop_adc() == 0);
        h.wrong_threshold = true;
        assert(b->start_adc() == ERANGE);
        assert(!(h.registers.at({1, 0x38}) & (1u << 16)));
    }
    h = Hardware{};
    h.fail_map = true;
    {
        auto b = open_test();
        double rate = 0;
        assert(b->configure(configuration(), rate) == 0);
        assert(b->prepare_dma(4, 1024) == ENOMEM);
        assert(!b->slot(0));
    }
    h = Hardware{};
    h.id = 0xc2e8; // prototype
    std::unique_ptr<aioaio::Backend> b;
    assert(aioaio::open_backend("/dev/apci/test-fixture", b) == ENOTSUP && !b);
    assert(h.closes == 1 && h.writes.empty());
    check_rate_limits();
    check_startup_diagnostics();
    std::puts("Native APCI register backend tests passed (all device calls stubbed).");
}
