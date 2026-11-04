#include "fake_backend.hpp"

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
const aioaio::Model test_model{0xC2EC, "Fake paired ADC", 2, 8, 16, 2, 4095, 1000000};
const aioaio::Model slow_model{0x805e, "mPCIe-ADI12-8E", 1, 8, 12, 2, 2047, 100000};
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    aioaio::Config config;
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
    state.cancelled = false;
    state.throw_start = state.throw_stop = false;
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
