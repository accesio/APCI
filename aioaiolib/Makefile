CC ?= cc
CXX ?= c++
INSTALL ?= install
PYTHON ?= python3
TEST_TIMEOUT ?= 20s
APCI_DIR ?= ..
BUILD_DIR ?= build
PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g

VERSION := 1.0.0
SONAME := libaioaio.so.1
LIBRARY := $(BUILD_DIR)/libaioaio.so.$(VERSION)
LIB_LINK := $(BUILD_DIR)/libaioaio.so
INCLUDES := -Iinclude -Isrc -I$(APCI_DIR)/apcilib
C_WARN := -Wall -Wextra -Wpedantic
CXX_WARN := -Wall -Wextra -Wpedantic
CORE_OBJ := $(BUILD_DIR)/aioaio.o $(BUILD_DIR)/dac_waveform.o
OBJECTS := $(CORE_OBJ) $(BUILD_DIR)/backend.o $(BUILD_DIR)/apcilib.o
SHARED_FLAGS := -shared -Wl,-soname,$(SONAME) -Wl,--version-script,exports.map -Wl,-z,defs

.PHONY: all check clean install FORCE
all: $(LIB_LINK) $(BUILD_DIR)/axio_benchmark $(BUILD_DIR)/check_dma $(BUILD_DIR)/axiodac $(BUILD_DIR)/axiodac_irq $(BUILD_DIR)/aioaio.pc

$(BUILD_DIR) $(BUILD_DIR)/tests:
	mkdir -p "$@"

$(BUILD_DIR)/%.o: src/%.cpp include/AIOAIO.h src/backend.hpp src/dac_backend.hpp src/dac_waveform.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -fPIC -fvisibility-inlines-hidden -pthread -MMD -MP -c "$<" -o "$@"

# apcilib is a private transport dependency; do not export its symbols.
$(BUILD_DIR)/apcilib.o: $(APCI_DIR)/apcilib/apcilib.c $(APCI_DIR)/apcilib/apcilib.h $(APCI_DIR)/apci_ioctl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -std=gnu11 -fPIC -fvisibility=hidden -MMD -MP -c "$<" -o "$@"

$(LIBRARY): $(OBJECTS) exports.map
	$(CXX) $(LDFLAGS) $(SHARED_FLAGS) -o "$@" $(OBJECTS) -pthread $(LDLIBS)

$(BUILD_DIR)/$(SONAME): $(LIBRARY)
	ln -sfn "libaioaio.so.$(VERSION)" "$(BUILD_DIR)/$(SONAME)"

$(LIB_LINK): $(BUILD_DIR)/$(SONAME)
	ln -sfn "$(SONAME)" "$@"

$(BUILD_DIR)/check_dma: examples/check_dma.c include/AIOAIO.h $(LIB_LINK)
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 "$<" -L"$(BUILD_DIR)" -laioaio -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/axio_benchmark: examples/axio_benchmark.c include/AIOAIO.h $(LIB_LINK)
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 "$<" -L"$(BUILD_DIR)" -laioaio -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/axiodac: examples/axiodac.c include/AIOAIO.h $(LIB_LINK)
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 "$<" -L"$(BUILD_DIR)" -laioaio -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/axiodac_irq: examples/axiodac_irq.c include/AIOAIO.h $(LIB_LINK)
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 "$<" -L"$(BUILD_DIR)" -laioaio -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LDLIBS) -lm -o "$@"

$(BUILD_DIR)/aioaio.pc: aioaio.pc.in Makefile FORCE | $(BUILD_DIR)
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@LIBDIR@|$(LIBDIR)|g' -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' -e 's|@VERSION@|$(VERSION)|g' "$<" > "$@"

FORCE:

$(BUILD_DIR)/tests/fake_backend.o: tests/fake_backend.cpp tests/fake_backend.hpp src/backend.hpp include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -fPIC -pthread -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/tests/test_engine: tests/test_engine.cpp tests/fake_backend.hpp $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/test_models: tests/test_models.cpp $(BUILD_DIR)/backend.o $(BUILD_DIR)/apcilib.o | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 "$<" $(BUILD_DIR)/backend.o $(BUILD_DIR)/apcilib.o $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/backend_registers: tests/backend_registers.cpp src/backend.hpp $(BUILD_DIR)/backend.o | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 "$<" $(BUILD_DIR)/backend.o -Wl,--wrap=open,--wrap=fstat,--wrap=flock,--wrap=close,--wrap=mmap,--wrap=munmap $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/backend_api: tests/backend_api.cpp src/backend.hpp $(BUILD_DIR)/backend.o | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(BUILD_DIR)/backend.o -Wl,--wrap=open,--wrap=fstat,--wrap=flock,--wrap=close,--wrap=mmap,--wrap=munmap $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/backend_dac: tests/backend_dac.cpp src/backend.hpp src/dac_backend.hpp $(BUILD_DIR)/backend.o $(CORE_OBJ) | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(BUILD_DIR)/backend.o $(CORE_OBJ) -Wl,--wrap=open,--wrap=fstat,--wrap=flock,--wrap=close,--wrap=mmap,--wrap=munmap $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/test_dac_waveform: tests/test_dac_waveform.cpp src/dac_waveform.hpp src/dac_backend.hpp $(CORE_OBJ) | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(CORE_OBJ) $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/example_main.o: examples/axio_benchmark.c include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 -Dmain=aio_sample_main -c "$<" -o "$@"

$(BUILD_DIR)/tests/example_runner: tests/example_runner.cpp tests/fake_backend.hpp $(BUILD_DIR)/tests/example_main.o $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(BUILD_DIR)/tests/example_main.o $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/check_dma_main.o: examples/check_dma.c include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 -Dmain=aio_sample_main -DCAPTURE_SAMPLES=13u -DTIMEOUT_SECONDS=0.05 -c "$<" -o "$@"

$(BUILD_DIR)/tests/sample_runner: tests/example_runner.cpp tests/fake_backend.hpp $(BUILD_DIR)/tests/check_dma_main.o $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $(CXX_WARN) -std=c++17 -pthread "$<" $(BUILD_DIR)/tests/check_dma_main.o $(CORE_OBJ) $(BUILD_DIR)/tests/fake_backend.o $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/axiodac_sample: examples/axiodac.c tests/dac_sample_stub.c include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 -DDURATION_SECONDS=0.001 "$<" tests/dac_sample_stub.c $(LDFLAGS) $(LDLIBS) -o "$@"

$(BUILD_DIR)/tests/axiodac_irq_sample: examples/axiodac_irq.c tests/dac_sample_stub.c include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -std=c11 -DDURATION_SECONDS=0.001 "$<" tests/dac_sample_stub.c $(LDFLAGS) $(LDLIBS) -lm -o "$@"

$(BUILD_DIR)/tests/header_c.o: tests/header_c.c include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Iinclude $(CFLAGS) $(C_WARN) -Werror -std=c11 -c "$<" -o "$@"

$(BUILD_DIR)/tests/header_cpp.o: tests/header_cpp.cpp include/AIOAIO.h | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) -Iinclude $(CXXFLAGS) $(CXX_WARN) -Werror -std=c++17 -c "$<" -o "$@"

# Engines use fake backends; transport tests stub APCI calls and system calls.
# No test opens hardware. The production library is inspected, not loaded.
check: all $(BUILD_DIR)/tests/header_c.o $(BUILD_DIR)/tests/header_cpp.o $(BUILD_DIR)/tests/test_engine $(BUILD_DIR)/tests/test_models $(BUILD_DIR)/tests/backend_registers $(BUILD_DIR)/tests/backend_api $(BUILD_DIR)/tests/example_runner $(BUILD_DIR)/tests/sample_runner $(BUILD_DIR)/tests/backend_dac $(BUILD_DIR)/tests/test_dac_waveform $(BUILD_DIR)/tests/axiodac_sample $(BUILD_DIR)/tests/axiodac_irq_sample
	$(PYTHON) tests/check_abi.py "$(LIBRARY)" exports.map
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/test_engine"
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/test_models"
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/backend_registers"
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/backend_api"
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/backend_dac"
	timeout "$(TEST_TIMEOUT)" "$(BUILD_DIR)/tests/test_dac_waveform"
	$(PYTHON) tests/check_example.py "$(BUILD_DIR)/tests/example_runner"
	$(PYTHON) tests/check_sample.py "$(BUILD_DIR)/tests/sample_runner"
	$(PYTHON) tests/check_dac_samples.py "$(BUILD_DIR)/tests/axiodac_sample" "$(BUILD_DIR)/tests/axiodac_irq_sample"

install: $(LIB_LINK) $(BUILD_DIR)/aioaio.pc
	$(INSTALL) -d "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(INCLUDEDIR)" "$(DESTDIR)$(PKGCONFIGDIR)"
	$(INSTALL) -m 755 "$(LIBRARY)" "$(DESTDIR)$(LIBDIR)/libaioaio.so.$(VERSION)"
	ln -sfn "libaioaio.so.$(VERSION)" "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn "$(SONAME)" "$(DESTDIR)$(LIBDIR)/libaioaio.so"
	$(INSTALL) -m 644 include/AIOAIO.h "$(DESTDIR)$(INCLUDEDIR)/AIOAIO.h"
	$(INSTALL) -m 644 "$(BUILD_DIR)/aioaio.pc" "$(DESTDIR)$(PKGCONFIGDIR)/aioaio.pc"

clean:
	rm -f "$(BUILD_DIR)"/*.o "$(BUILD_DIR)"/*.d "$(BUILD_DIR)"/libaioaio.so* "$(BUILD_DIR)/check_dma" "$(BUILD_DIR)/axio_benchmark" "$(BUILD_DIR)/axiodac" "$(BUILD_DIR)/axiodac_irq" "$(BUILD_DIR)/aioaio.pc"
	rm -f "$(BUILD_DIR)/tests"/*.o "$(BUILD_DIR)/tests"/*.d "$(BUILD_DIR)/tests/test_engine" "$(BUILD_DIR)/tests/test_models"
	rm -f "$(BUILD_DIR)/tests/example_runner" "$(BUILD_DIR)/tests/sample_runner" "$(BUILD_DIR)/tests/backend_registers" "$(BUILD_DIR)/tests/backend_dac" "$(BUILD_DIR)/tests/test_dac_waveform"
	rm -f "$(BUILD_DIR)/tests/backend_api"
	rm -f "$(BUILD_DIR)/tests/axiodac_sample" "$(BUILD_DIR)/tests/axiodac_irq_sample"

-include $(OBJECTS:.o=.d) $(BUILD_DIR)/tests/fake_backend.d
