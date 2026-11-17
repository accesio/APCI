#include "fake_backend.hpp"

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
const aioaio::Model test_model{0xC2EC, "Fake paired ADC", 2, 8, 16, 2, 4095, 1000000};
const aioaio::Model slow_model{0x805e, "mPCIe-ADI12-8E", 1, 8, 12, 2, 2047, 100000};
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    aioaio::Config config, immediate_config;
    std::map<uint32_t, uint32_t> registers;
    std::vector<fake::RegisterOp> register_ops;
    uint32_t resets = 0;
    int reset_error = 0;
    std::vector<std::vector<uint32_t>> slots;
    std::deque<uint32_t> ready;
    uint32_t next_slot = 0;
    uint32_t acknowledged = 0;
    uint32_t starts = 0;
    uint32_t stops = 0;
    uint32_t opens = 0;
    uint32_t cancels = 0;
    uint32_t ignored_cancels = 0;
    uint32_t discarded = 0;
    int ready_error = 0;
    bool started = false;
    bool cancelled = false;
    bool throw_start = false;
    bool throw_stop = false;
} state;

class FakeBackend final : public aioaio::Backend {
    std::string path_;
    const aioaio::Model &model_;
public:
    explicit FakeBackend(std::string path) : path_(std::move(path)),
        model_(path_ == "fake:slow" ? slow_model : test_model) {}
    const aioaio::Model &model() const noexcept override { return model_; }
    const std::string &path() const noexcept override { return path_; }
    int configure(const aioaio::Config &config, double &actual) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.config = config;
        actual = config.rate_hz;
        return 0;
    }
    int reset_board() override {
        std::lock_guard<std::mutex> lock(state.mutex);
        ++state.resets;
        state.registers.clear();
        state.ready.clear();
        if (!state.reset_error) state.ready_error = 0;
        return state.reset_error;
    }
    int read_register(uint32_t width, uint32_t offset, uint32_t &value) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        if ((width != 1 && width != 2 && width != 4) || offset % width || offset > 0xff) return EINVAL;
        value = state.registers[offset] & (width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1);
        state.register_ops.push_back({width, offset, value, false});
        return 0;
    }
    int write_register(uint32_t width, uint32_t offset, uint32_t value) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        if ((width != 1 && width != 2 && width != 4) || offset % width || offset > 0xff) return EINVAL;
        state.registers[offset] = value;
        state.register_ops.push_back({width, offset, value, true});
        return 0;
    }
    int read_register_block(uint32_t width, uint32_t offset, uint32_t count, void *data) override {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            const int error = read_register(width, offset, value);
            if (error) return error;
            std::memcpy(static_cast<unsigned char *>(data) + i * width, &value, width);
        }
        return 0;
    }
    int write_register_block(uint32_t width, uint32_t offset, uint32_t count, const void *data) override {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            std::memcpy(&value, static_cast<const unsigned char *>(data) + i * width, width);
            const int error = write_register(width, offset, value);
            if (error) return error;
        }
        return 0;
    }
    int immediate_scan(const aioaio::Config &config, uint32_t *words, uint32_t &count) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.immediate_config = config;
        const uint32_t per_adc = config.differential ? 4 : 8;
        count = per_adc * model_.adc_count;
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc)
            for (uint32_t ch = 0; ch < per_adc; ++ch) {
                const uint32_t physical = adc * 8 + ch * (config.differential ? 2 : 1);
                words[adc * per_adc + ch] = (physical << 20) |
                    (config.range_codes[physical] << 16) | (physical ? 0x4000u : 0x8000u);
            }
        return 0;
    }
    int prepare_dma(uint32_t count, uint32_t rows) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.slots.assign(count, std::vector<uint32_t>(rows * model_.raw_words_per_row));
        state.ready.clear();
        // Start at the final slot to exercise the ring-wrap path immediately.
        state.next_slot = count - 1;
        state.cancelled = false;
        return 0;
    }
    int start_adc() override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.started = true;
        ++state.starts;
        state.cv.notify_all();
        if (state.throw_start) throw std::runtime_error("fake start exception");
        return 0;
    }
    int stop_adc() override {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.started = false;
        ++state.stops;
        state.cv.notify_all();
        if (state.throw_stop) throw std::runtime_error("fake stop exception");
        return 0;
    }
    int ready(uint32_t &start, uint32_t &count, uint32_t &discarded) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        start = state.ready.empty() ? 0 : state.ready.front();
        count = static_cast<uint32_t>(state.ready.size());
        discarded = state.discarded;
        state.discarded = 0;
        return state.ready_error;
    }
    int acknowledge(uint32_t count) override {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (count > state.ready.size()) return EINVAL;
        for (uint32_t i = 0; i < count; ++i) state.ready.pop_front();
        state.acknowledged += count;
        state.cv.notify_all();
        return 0;
    }
    const uint32_t *slot(uint32_t index) const noexcept override {
        return index < state.slots.size() ? state.slots[index].data() : nullptr;
    }
    int wait_irq() override {
        std::unique_lock<std::mutex> lock(state.mutex);
        const bool woke = state.cv.wait_for(lock, std::chrono::seconds(3), [] {
            return state.cancelled || !state.ready.empty() || state.discarded || state.ready_error;
        });
        if (!woke) return ETIMEDOUT;
        return state.cancelled ? ECANCELED : 0;
    }
    int cancel_irq() override {
        std::lock_guard<std::mutex> lock(state.mutex);
        ++state.cancels;
        if (state.ignored_cancels) {
            --state.ignored_cancels;
            return 0;
        }
        state.cancelled = true;
        state.cv.notify_all();
        return 0;
    }
};
} // namespace

namespace aioaio {
const Model *find_model(uint32_t id) noexcept {
    if (id == slow_model.device_id) return &slow_model;
    return id == test_model.device_id ? &test_model : nullptr;
}
int open_backend(const std::string &path, std::unique_ptr<Backend> &backend) {
    std::lock_guard<std::mutex> lock(state.mutex);
    ++state.opens;
    if (path == "fake:unsupported") return ENOTSUP;
    backend = std::make_unique<FakeBackend>(path);
    return 0;
}
} // namespace aioaio

namespace fake {
void reset() {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.started) throw std::logic_error("reset while fake acquisition is active");
    state.ready.clear();
    state.acknowledged = state.starts = state.stops = state.discarded = 0;
    state.cancels = state.ignored_cancels = 0;
    state.ready_error = 0;
    state.reset_error = 0;
    state.resets = 0;
    state.registers.clear();
    state.register_ops.clear();
    state.cancelled = false;
    state.throw_start = state.throw_stop = false;
}
std::vector<RegisterOp> register_operations() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.register_ops;
}
void fail_reset(int error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.reset_error = error;
}
uint32_t reset_count() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.resets;
}
aioaio::Config last_immediate_config() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.immediate_config;
}
uint32_t sample_word(uint32_t sample) {
    const uint32_t channel = (sample / 2) % 8 + (sample % 2) * 8;
    return (channel << 20) | (7u << 16) | (sample & 0xffffu);
}
void emit_slots(uint32_t count, uint32_t first_sample) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.started || count + state.ready.size() > state.slots.size())
        throw std::logic_error("invalid fake DMA emission");
    for (uint32_t i = 0; i < count; ++i) {
        auto &slot = state.slots[state.next_slot];
        for (auto &word : slot) word = sample_word(first_sample++);
        state.ready.push_back(state.next_slot);
        state.next_slot = (state.next_slot + 1) % state.slots.size();
    }
    state.cv.notify_all();
}
void emit_words(const std::vector<uint32_t> &words) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.started || state.ready.size() == state.slots.size() ||
        words.size() > state.slots[state.next_slot].size())
        throw std::logic_error("invalid fake DMA words");
    auto &slot = state.slots[state.next_slot];
    std::fill(slot.begin(), slot.end(), 0x80000000u);
    std::copy(words.begin(), words.end(), slot.begin());
    state.ready.push_back(state.next_slot);
    state.next_slot = (state.next_slot + 1) % state.slots.size();
    state.cv.notify_all();
}
void fail_ready(int error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.ready_error = error;
    state.cv.notify_all();
}
void discard_slots(uint32_t count) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.discarded = count;
    state.cv.notify_all();
}
void ignore_cancels(uint32_t count) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.ignored_cancels = count;
}
void throw_on_start_and_stop(bool start, bool stop) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.throw_start = start;
    state.throw_stop = stop;
}
uint32_t cancel_count() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.cancels;
}
uint32_t words_per_slot() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.slots.empty() ? 0 : static_cast<uint32_t>(state.slots[0].size());
}
uint32_t acknowledgements() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.acknowledged;
}
uint32_t start_count() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.starts;
}
uint32_t stop_count() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.stops;
}
uint32_t open_count() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.opens;
}
aioaio::Config last_config() {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.config;
}
bool wait_started(unsigned milliseconds) {
    std::unique_lock<std::mutex> lock(state.mutex);
    return state.cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [] { return state.started; });
}
bool wait_acknowledged(uint32_t count, unsigned milliseconds) {
    std::unique_lock<std::mutex> lock(state.mutex);
    return state.cv.wait_for(lock, std::chrono::milliseconds(milliseconds), [count] { return state.acknowledged >= count; });
}
} // namespace fake
