#pragma once

#include "backend.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fake {
// Controls and inspection for a deterministic, entirely in-memory device.
void reset();
void emit_slots(uint32_t count, uint32_t first_sample = 0);
void emit_words(const std::vector<uint32_t> &words);
void fail_ready(int error);
void discard_slots(uint32_t count);
void ignore_cancels(uint32_t count);
void throw_on_start_and_stop(bool start, bool stop);
uint32_t cancel_count();
uint32_t sample_word(uint32_t sample);
uint32_t words_per_slot();
uint32_t acknowledgements();
uint32_t start_count();
uint32_t stop_count();
uint32_t open_count();
aioaio::Config last_config();
bool wait_started(unsigned milliseconds = 2000);
bool wait_acknowledged(uint32_t count, unsigned milliseconds = 2000);
} // namespace fake
