#include "backend.hpp"
#include "apcilib.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aioaio {
namespace {

// APCI maps physical PCI BAR2 into logical region 1 for native AxIO.
constexpr int registers = 1;
constexpr uint32_t clock_offset = 0x0c;
constexpr uint32_t divisor_offset = 0x10;
constexpr uint32_t gains_offset = 0x18;
constexpr uint32_t threshold_offset = 0x20;
constexpr uint32_t fifo_count_offset = 0x28;
constexpr uint32_t control_offset = 0x38;
constexpr uint32_t irq_offset = 0x40;
constexpr uint32_t dio_control_offset = 0x48;
constexpr uint32_t go = 1u << 16;
constexpr uint32_t load_config = 1u << 17;
constexpr uint32_t adc_irq_mask = 0x003f;
constexpr uint32_t dma_irqs = (1u << 0) | (1u << 2);

// IDs and chip counts come from CoreExports.pas and APCI apci_dev.h.
// Rate limits are per-ADC product ratings; see README for sources and the
// existing DAAI device-ID/firmware mapping caveat.
// Native ADIO register maps document an 11-bit FIFO threshold. Use the
// conservative limit even on firmware with larger physical FIFOs.
#define M(id, name, count, bits, max_rate) {id, name, count, 8, bits, 2, 2047, max_rate}
constexpr Model models[] = {
    M(0xc0e8, "mPCIe-AIO16-16F", 2, 16, 1000000),
    M(0xc0e9, "mPCIe-AIO16-16A", 2, 16, 500000),
    M(0xc0ea, "mPCIe-AIO16-16E", 2, 16, 250000),
    M(0xc0eb, "mPCIe-AIO16-16FDS", 2, 16, 1000000),
    M(0x80e8, "mPCIe-AI16-16F", 2, 16, 1000000),
    M(0x80e9, "mPCIe-AI16-16A", 2, 16, 500000),
    M(0x80ea, "mPCIe-AI16-16E", 2, 16, 250000),
    M(0xc058, "mPCIe-AIO12-16A", 2, 12, 500000),
    M(0xc059, "mPCIe-AIO12-16", 2, 12, 250000),
    M(0xc05a, "mPCIe-AIO12-16E", 2, 12, 100000),
    M(0x8058, "mPCIe-AI12-16A", 2, 12, 500000),
    M(0x8059, "mPCIe-AI12-16", 2, 12, 250000),
    M(0x805a, "mPCIe-AI12-16E", 2, 12, 100000),
    M(0xc2ec, "PCIe-ADIO16-16F", 2, 16, 1000000),
    M(0xc2ed, "PCIe-ADIO16-16A", 2, 16, 500000),
    M(0xc2ee, "PCIe-ADIO16-16E", 2, 16, 250000),
    M(0xc2ef, "PCIe-ADIO16-16FDS", 2, 16, 1000000),
    M(0x82ec, "PCIe-ADI16-16F", 2, 16, 1000000),
    M(0x82ed, "PCIe-ADI16-16A", 2, 16, 500000),
    M(0x82ee, "PCIe-ADI16-16E", 2, 16, 250000),
    M(0xc25c, "PCIe-ADIO12-16A", 2, 12, 500000),
    M(0xc25d, "PCIe-ADIO12-16", 2, 12, 250000),
    M(0xc25e, "PCIe-ADIO12-16E", 2, 12, 100000),
    M(0x825c, "PCIe-ADI12-16A", 2, 12, 500000),
    M(0x825d, "PCIe-ADI12-16", 2, 12, 250000),
    M(0x825e, "PCIe-ADI12-16E", 2, 12, 100000),
    M(0xc0ec, "mPCIe-ADIO16-8F", 1, 16, 1000000),
    M(0xc0ed, "mPCIe-ADIO16-8A", 1, 16, 500000),
    M(0xc0ee, "mPCIe-ADIO16-8E", 1, 16, 250000),
    M(0xc0ef, "mPCIe-ADIO16-8FDS", 1, 16, 1000000),
    M(0x80ec, "mPCIe-ADI16-8F", 1, 16, 1000000),
    M(0x80ed, "mPCIe-ADI16-8A", 1, 16, 500000),
    M(0x80ee, "mPCIe-ADI16-8E", 1, 16, 250000),
    M(0xc05c, "mPCIe-ADIO12-8A", 1, 12, 500000),
    M(0xc05d, "mPCIe-ADIO12-8", 1, 12, 250000),
    M(0xc05e, "mPCIe-ADIO12-8E", 1, 12, 100000),
    M(0x805c, "mPCIe-ADI12-8A", 1, 12, 500000),
    M(0x805d, "mPCIe-ADI12-8", 1, 12, 250000),
    M(0x805e, "mPCIe-ADI12-8E", 1, 12, 100000),
    M(0xcee8, "mPCIe-DAAI16-8F", 1, 16, 1000000),
    M(0xcee9, "mPCIe-DAAI16-8A", 1, 16, 500000),
    M(0xceea, "mPCIe-DAAI16-8E", 1, 16, 250000),
    M(0xce50, "mPCIe-DAAI12-8", 1, 12, 250000),
    M(0xce51, "mPCIe-DAAI12-8E", 1, 12, 100000),
    M(0xce52, "mPCIe-DAAI12-8A", 1, 12, 500000),
    M(0xced8, "mPCIe-DAAI16-4F", 1, 16, 1000000),
    M(0xced9, "mPCIe-DAAI16-4A", 1, 16, 500000),
    M(0xceda, "mPCIe-DAAI16-4E", 1, 16, 250000),
    M(0xce98, "mPCIe-DAAI12-4A", 1, 12, 500000),
    M(0xce9a, "mPCIe-DAAI12-4", 1, 12, 250000),
    M(0xce9b, "mPCIe-DAAI12-4E", 1, 12, 100000),
};
#undef M

int status_error(int status) noexcept {
    if (status == 0) return 0;
    return status < 0 && errno ? errno : EIO;
}

class ApciBackend final : public Backend {
public:
    ApciBackend(int descriptor, const Model &model, std::string path)
        : fd_(descriptor), model_(model), path_(std::move(path)) {
        const char *setting = std::getenv("AIOAIO_DEBUG");
        debug_ = setting && std::strcmp(setting, "1") == 0;
        trace("opened %s, PCI device ID 0x%04x", model_.name, model_.device_id);
    }
    ~ApciBackend() override {
        if (touched_) (void)stop_adc();
        unmap();
        // APCI owns the coherent allocation beyond close. Do not resize it to
        // zero: that ioctl assumes a valid nonzero ring and can race its ISR.
        ::close(fd_);
    }

    const Model &model() const noexcept override { return model_; }
    const std::string &path() const noexcept override { return path_; }

    int configure(const Config &config, double &actual_rate) override {
        if (running_) return EBUSY;
        const uint32_t limit = config.differential ? 4 : 8;
        if (config.channels_per_adc == 0 || config.channels_per_adc > limit ||
            !std::isfinite(config.rate_hz) || config.rate_hz <= 0) return EINVAL;
        if (config.rate_hz > model_.max_adc_rate_hz) {
            trace("requested ADC rate %.9g exceeds %s maximum of %u conversions/s/ADC",
                  config.rate_hz, model_.name, model_.max_adc_rate_hz);
            return ERANGE;
        }
        if (debug_ && !reported_firmware_) {
            // Diagnostics must not turn an optional version read into an
            // acquisition prerequisite, especially on older firmware.
            uint32_t version = 0;
            errno = 0;
            const int error = status_error(apci_read32(fd_, 0, registers, 0x68, &version));
            if (error) trace("FPGA revision at logical BAR[1]+0x68: unavailable: %s (%d)",
                             std::strerror(error), error);
            else trace("FPGA revision at logical BAR[1]+0x68: 0x%08x (raw)", version);
            reported_firmware_ = true;
        }
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc)
            for (uint32_t ch = 0; ch < config.channels_per_adc; ++ch) {
                const uint32_t input = config.differential ? ch * 2 : ch;
                const uint32_t range = config.range_codes[adc * 8 + input];
                if (range > 7 || range == 6) return EINVAL;
            }
        uint32_t base_clock = 0;
        int error = read(clock_offset, base_clock);
        if (error) return error;
        if (!base_clock) {
            trace("FAILED ADC base clock at logical BAR[1]+0x0c: expected a nonzero clock, got 0");
            return EIO;
        }
        const double rounded = std::round(static_cast<double>(base_clock) / config.rate_hz);
        if (rounded < 1 || rounded > std::numeric_limits<uint32_t>::max()) return ERANGE;
        // Round to the nearest divider unless that would exceed this model's
        // rated rate. This also handles clocks not divisible by the maximum.
        const uint32_t minimum_divisor = static_cast<uint32_t>(
            (static_cast<uint64_t>(base_clock) + model_.max_adc_rate_hz - 1) / model_.max_adc_rate_hz);
        config_ = config;
        divisor_ = std::max(static_cast<uint32_t>(rounded), minimum_divisor);
        actual_rate = static_cast<double>(base_clock) / divisor_;
        actual_rate_ = actual_rate;
        configured_ = true;
        return 0;
    }

    int prepare_dma(uint32_t slots, uint32_t rows) override {
        trace("prepare DMA: %u slots, %u FIFO rows per slot", slots, rows);
        if (!configured_) return EINVAL;
        if (running_) return EBUSY;
        if (slots < 2 || slots > 255 || rows == 0 || rows > model_.max_fifo_rows)
            return EINVAL;
        // Reject a board already being driven by a noncooperating application.
        uint32_t control = 0;
        int error = read(control_offset, control);
        if (error) return error;
        if (control & go) return EBUSY;
        uint32_t dio_control = 0;
        if ((error = read(dio_control_offset, dio_control))) return error;
        // External-start/trigger configuration changes the raw DMA format and
        // timing contract. Do not silently alter the application's DIO setup.
        if (dio_control & ((1u << 16) | (1u << 18))) return ENOTSUP;
        touched_ = true;
        if ((error = stop_adc())) return error;
        unmap();
        const size_t slot_bytes = static_cast<size_t>(rows) * model_.raw_words_per_row * sizeof(uint32_t);
        errno = 0;
        error = status_error(apci_dma_transfer_size(fd_, 0, static_cast<__u8>(slots), slot_bytes));
        if (error) {
            trace("FAILED apci_dma_transfer_size: %s (%d)", std::strerror(error), error);
            return error;
        }
        const size_t total_bytes = slot_bytes * slots;
        void *memory = ::mmap(nullptr, total_bytes, PROT_READ, MAP_SHARED, fd_, 0);
        if (memory == MAP_FAILED) {
            error = errno ? errno : EIO;
            trace("FAILED mmap DMA (%zu bytes): %s (%d)", total_bytes, std::strerror(error), error);
            return error;
        }
        mapping_ = memory;
        mapping_bytes_ = total_bytes;
        slot_bytes_ = slot_bytes;
        slots_ = slots;
        rows_ = rows;
        return 0;
    }

    int start_adc() override {
        trace("start ADC");
        if (running_) return EBUSY;
        if (!configured_ || mapping_ == MAP_FAILED) return EINVAL;
        int error = 0;
        if ((error = clear_fifo())) return error;
        if ((error = write(threshold_offset, rows_))) return error;
        uint32_t value = 0;
        if ((error = read(threshold_offset, value))) return error;
        if ((error = check_readback("ADC FIFO threshold", threshold_offset, value, rows_, 0xffffffffu, ERANGE))) return error;
        if ((error = write(divisor_offset, divisor_))) return error;
        if ((error = read(divisor_offset, value))) return error;
        if ((error = check_readback("ADC clock divisor", divisor_offset, value, divisor_))) return error;

        // Native FPGA channel mode (bit18=0) clocks one conversion/ADC per
        // divisor. ADAS advanced sequencing still cycles inputs; +0x14 is
        // ignored in this FPGA mode (native register-map description).
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc) {
            uint32_t gains = 0;
            for (uint32_t ch = 0; ch < config_.channels_per_adc; ++ch) {
                const uint32_t input = config_.differential ? ch * 2 : ch;
                gains |= config_.range_codes[adc * 8 + input] << (input * 4);
            }
            if ((error = write(gains_offset + adc * 4, gains))) return error;
            if ((error = read(gains_offset + adc * 4, value))) return error;
            if ((error = check_readback("ADC gain", gains_offset + adc * 4, value, gains))) return error;
        }
        const uint32_t last = (config_.channels_per_adc - 1) * (config_.differential ? 2 : 1);
        // CFG, MUX, advanced SEQ, TEMPB, REFEN and CPHA. CMS=normal at <=900k.
        control_ = 0x0000806d | (last << 12);
        if (!config_.differential) control_ |= 1u << 11;
        if (actual_rate_ <= 900000.0) control_ |= 1u << 1;
        // ADC1's independent lower configuration must precede ADC0. Timed
        // acquisitions mirror ADC0's high control bits onto ADC1 in hardware.
        if (model_.adc_count == 2)
            if ((error = write(control_offset + 4, control_))) return error;
        if ((error = write(control_offset, control_ | load_config))) return error;
        if ((error = read(control_offset, value))) return error;
        constexpr uint32_t configuration_mask = 0x00007bfa;
        if ((error = check_readback("ADC configuration", control_offset, value, control_, configuration_mask))) return error;
        if (model_.adc_count == 2) {
            if ((error = read(control_offset + 4, value))) return error;
            if ((error = check_readback("ADC configuration", control_offset + 4, value, control_, configuration_mask))) return error;
        }
        uint32_t irq = 0;
        if ((error = read(irq_offset, irq))) return error;
        // High status bits are W1C: clear only acquisition-owned statuses.
        if ((error = write(irq_offset, (irq & 0xffffu & ~adc_irq_mask) |
                                      dma_irqs | (adc_irq_mask << 16)))) return error;
        running_ = true; // Also clean up a partially successful GO write.
        error = write(control_offset, control_ | load_config | go);
        if (error) { (void)stop_adc(); return error; }
        if ((error = read(control_offset, value))) { (void)stop_adc(); return error; }
        if ((error = check_readback("ADC GO", control_offset, value, control_ | load_config | go, go))) {
            (void)stop_adc();
            return error;
        }
        return 0;
    }

    int stop_adc() override {
        if (!touched_) return 0;
        trace("stop ADC");
        int first_error = 0;
        auto remember = [&first_error](int error) { if (!first_error) first_error = error; };
        uint32_t value = 0;
        int error = read(control_offset, value);
        remember(error);
        if (!error) remember(write(control_offset, value & ~go & ~load_config));
        error = read(irq_offset, value);
        remember(error);
        if (!error) remember(write(irq_offset, (value & 0xffffu & ~adc_irq_mask) |
                                             (adc_irq_mask << 16)));
        // BAR0+0x1c bit3 aborts ADC DMA and clears its FIFO. This is the
        // operation used by APCI itself in apci_set_dma_transfer_size.
        errno = 0;
        error = status_error(apci_write8(fd_, 0, 0, 0x1c, 0x08));
        trace("%swrite8 logical BAR[0]+0x1c = 0x08: %s (%d)",
              error ? "FAILED " : "", error ? std::strerror(error) : "ok", error);
        remember(error);
        remember(read_bar(0, 0x1c, value)); // flush posted write
        remember(clear_fifo());
        running_ = false;
        return first_error;
    }

    int ready(uint32_t &start, uint32_t &count, uint32_t &discarded) override {
        start = count = discarded = 0;
        if (mapping_ == MAP_FAILED) return EINVAL;
        int s = 0, n = 0, d = 0;
        errno = 0;
        const int error = status_error(apci_dma_data_ready(fd_, 0, &s, &n, &d));
        if (error) return error;
        if (n < 0 || d < 0 || static_cast<uint32_t>(n) >= slots_ ||
            (n && (s < 0 || static_cast<uint32_t>(s) >= slots_))) return EIO;
        start = n ? static_cast<uint32_t>(s) : 0;
        count = static_cast<uint32_t>(n);
        discarded = static_cast<uint32_t>(d);
        outstanding_ = count;
        return 0;
    }

    int acknowledge(uint32_t count) override {
        if (count == 0) return 0;
        if (count > outstanding_ || count >= slots_) return EINVAL;
        errno = 0;
        const int error = status_error(apci_dma_data_done(fd_, 0, static_cast<int>(count)));
        if (!error) outstanding_ -= count;
        return error;
    }

    const uint32_t *slot(uint32_t index) const noexcept override {
        if (mapping_ == MAP_FAILED || index >= slots_) return nullptr;
        return reinterpret_cast<const uint32_t *>(static_cast<const unsigned char *>(mapping_) + slot_bytes_ * index);
    }
    int wait_irq() override {
        errno = 0;
        return status_error(apci_wait_for_irq(fd_, 0));
    }
    int cancel_irq() override {
        errno = 0;
        return status_error(apci_cancel_irq(fd_, 0));
    }

private:
    // Only setup/cleanup register traffic is traced, never sample contents.
    // Preserve errno so diagnostics cannot change an API's error result.
    void trace(const char *format, ...) const noexcept {
        if (!debug_) return;
        const int saved_errno = errno;
        ::flockfile(stderr);
        std::fprintf(stderr, "libaioaio [%s]: ", path_.c_str());
        va_list args;
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        va_end(args);
        std::fputc('\n', stderr);
        ::funlockfile(stderr);
        errno = saved_errno;
    }
    int check_readback(const char *name, uint32_t offset, uint32_t actual,
                       uint32_t expected, uint32_t mask = 0xffffffffu,
                       int error = EIO) const noexcept {
        if ((actual & mask) == (expected & mask)) return 0;
        trace("FAILED %s at logical BAR[1]+0x%02x: expected 0x%08x, got 0x%08x, mask 0x%08x",
              name, offset, expected, actual, mask);
        return error;
    }
    int clear_fifo() {
        // Native AxIO maps define reset bit6 as ADC FIFO-only reset. Preserve
        // level controls such as DAAI HART reset; never assert SYS/DAC/DIO reset.
        // Some maps explicitly call bit6 self-clearing. Deassert it explicitly
        // as well so older implementations cannot leave the FIFO held reset.
        uint32_t resets = 0;
        int error = read(0, resets);
        if (error) return error;
        if (resets & 0x3fu) return EBUSY; // another reset/powerdown is active
        if ((error = write(0, resets | 0x40u))) return error;
        if ((error = write(0, resets & ~0x40u))) return error;
        uint32_t depth = 0;
        if ((error = read(fifo_count_offset, depth))) return error;
        return check_readback("ADC FIFO empty", fifo_count_offset, depth, 0);
    }
    int read(uint32_t offset, uint32_t &value) {
        return read_bar(registers, offset, value);
    }
    int read_bar(int bar, uint32_t offset, uint32_t &value) {
        errno = 0;
        const int error = status_error(apci_read32(fd_, 0, bar, static_cast<int>(offset), &value));
        if (error) trace("FAILED read32 logical BAR[%d]+0x%02x: %s (%d)",
                         bar, offset, std::strerror(error), error);
        else trace("read32 logical BAR[%d]+0x%02x = 0x%08x", bar, offset, value);
        return error;
    }
    int write(uint32_t offset, uint32_t value) {
        errno = 0;
        const int error = status_error(apci_write32(fd_, 0, registers, static_cast<int>(offset), value));
        trace("%swrite32 logical BAR[1]+0x%02x = 0x%08x: %s (%d)",
              error ? "FAILED " : "", offset, value, error ? std::strerror(error) : "ok", error);
        return error;
    }
    void unmap() noexcept {
        if (mapping_ != MAP_FAILED) ::munmap(mapping_, mapping_bytes_);
        mapping_ = MAP_FAILED;
        mapping_bytes_ = slot_bytes_ = 0;
        slots_ = rows_ = outstanding_ = 0;
    }
    int fd_;
    const Model &model_;
    std::string path_;
    Config config_;
    uint32_t divisor_ = 0, control_ = 0, slots_ = 0, rows_ = 0, outstanding_ = 0;
    double actual_rate_ = 0;
    void *mapping_ = MAP_FAILED;
    size_t mapping_bytes_ = 0, slot_bytes_ = 0;
    bool configured_ = false, touched_ = false, running_ = false;
    bool debug_ = false, reported_firmware_ = false;
};

} // namespace

const Model *find_model(uint32_t id) noexcept {
    for (const auto &model : models) if (model.device_id == id) return &model;
    // Prototypes, DA-only and specialized DF/HC variants are deliberately
    // excluded until their ADC capability/range contract has been verified.
    return nullptr;
}

int open_backend(const std::string &path, std::unique_ptr<Backend> &backend) {
    if (path.empty() || path.find('\0') != std::string::npos) return EINVAL;
    int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return errno;
    auto fail = [fd](int error) { ::close(fd); return error; };
    struct stat st{};
    if (::fstat(fd, &st) < 0) return fail(errno);
    if (!S_ISCHR(st.st_mode)) return fail(ENODEV);
    if (::flock(fd, LOCK_EX | LOCK_NB) < 0) return fail(errno == EWOULDBLOCK ? EBUSY : errno);
    unsigned int id = 0;
    errno = 0;
    const int error = status_error(apci_get_device_info(fd, 0, &id, nullptr));
    if (error) return fail(error);
    const Model *model = find_model(id);
    if (!model) return fail(ENOTSUP);
    try {
        backend = std::make_unique<ApciBackend>(fd, *model, path);
    } catch (...) {
        ::close(fd);
        throw;
    }
    return 0;
}

} // namespace aioaio
