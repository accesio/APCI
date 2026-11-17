#include "backend.hpp"
#include <iostream>

int main() {
    // Pure model lookup only. Never call open_backend or hardware operations.
    struct Expected { uint32_t id, adcs, bits, max_rate; };
    for (const auto model : {Expected{0xc2ec, 2, 16, 1000000}, Expected{0xc0e8, 2, 16, 1000000},
                             Expected{0xc25c, 2, 12, 500000}, Expected{0xc0ec, 1, 16, 1000000},
                             Expected{0x805d, 1, 12, 250000}, Expected{0xcee8, 1, 16, 1000000},
                             Expected{0x805e, 1, 12, 100000}, Expected{0xc05e, 1, 12, 100000},
                             Expected{0xc0ed, 1, 16, 500000}, Expected{0x80ee, 1, 16, 250000},
                             Expected{0xc0eb, 2, 16, 1000000}, Expected{0xce9b, 1, 12, 100000}}) {
        const auto *found = aioaio::find_model(model.id);
        if (!found || found->adc_count != model.adcs || found->resolution_bits != model.bits ||
            found->raw_words_per_row != 2 || found->channels_per_adc != 8 ||
            found->max_adc_rate_hz != model.max_rate) return 1;
    }
    // DAC population is independent of ADC resolution and timed FIFO support.
    for (uint32_t id : {0xc0e8u, 0xc0e9u, 0xc0eau, 0xc0ebu, 0xc058u, 0xc059u, 0xc05au,
                       0xc2ecu, 0xc2edu, 0xc2eeu, 0xc2efu, 0xc25cu, 0xc25du, 0xc25eu,
                       0xc0ecu, 0xc0edu, 0xc0eeu, 0xc0efu, 0xc05cu, 0xc05du, 0xc05eu}) {
        const auto *m = aioaio::find_model(id);
        if (!m || m->dac_chip != aioaio::DacChip::ltc2664 || m->dac_count != 4) return 3;
    }
    for (uint32_t id : {0xcee8u, 0xcee9u, 0xceeau, 0xce50u, 0xce51u, 0xce52u,
                       0xced8u, 0xced9u, 0xcedau, 0xce98u, 0xce9au, 0xce9bu}) {
        const auto *m = aioaio::find_model(id);
        const uint32_t count = (id >= 0xcee8 || (id >= 0xce50 && id <= 0xce52)) ? 8 : 4;
        if (!m || m->dac_chip != aioaio::DacChip::ad5755 || m->dac_count != count) return 4;
    }
    for (uint32_t id : {0x80e8u, 0x80e9u, 0x80eau, 0x8058u, 0x8059u, 0x805au,
                       0x82ecu, 0x82edu, 0x82eeu, 0x825cu, 0x825du, 0x825eu,
                       0x80ecu, 0x80edu, 0x80eeu, 0x805cu, 0x805du, 0x805eu}) {
        const auto *m = aioaio::find_model(id);
        if (!m || m->dac_chip != aioaio::DacChip::none || m->dac_count != 0) return 5;
    }
    // Prototype and non-ADC device IDs must not silently use a native model.
    for (const uint32_t id : {0xc2e8u, 0xc2e9u, 0xc2eau, 0x82e8u, 0x82e9u, 0x82eau,
                              0xc258u, 0xc259u, 0xc25au, 0x8258u, 0x8259u, 0x825au,
                              0x4ce8u, 0x4cd8u, 0xffffu}) {
        if (aioaio::find_model(id)) return 2;
    }
    std::cout << "Models: native geometry, ADC rate limits, and prototype exclusion verified without opening devices\n";
}
