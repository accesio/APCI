#include "fake_backend.hpp"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

extern "C" int aio_sample_main(int, char **);

int main(int argc, char **argv) {
    const char *mode = std::getenv("FAKE_MODE");
    std::atomic<bool> finished{false}, producer_failed{false};
    std::thread producer([&] {
        try {
            while (!finished.load()) {
                if (!fake::wait_started(10)) continue;
                if (mode && !std::strcmp(mode, "timeout")) return;
                if (mode && !std::strcmp(mode, "signal")) { std::raise(SIGINT); return; }
                if (mode && !std::strcmp(mode, "drop")) { fake::discard_slots(2); return; }
                fake::emit_slots(1);
                return;
            }
        } catch (...) {
            producer_failed.store(true);
        }
    });
    const int result = aio_sample_main(argc, argv);
    finished.store(true);
    producer.join();
    if (producer_failed.load()) {
        std::cerr << "fake producer failed\n";
        return 3;
    }
    if (std::getenv("EXPECT_NO_OPEN") && fake::open_count()) {
        std::cerr << "CLI unexpectedly opened a device\n";
        return 3;
    }
    if (std::getenv("EXPECT_NO_START") && fake::start_count()) {
        std::cerr << "CLI unexpectedly started acquisition\n";
        return 3;
    }
    return result;
}
