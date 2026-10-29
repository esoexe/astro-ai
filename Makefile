CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
TARGET := workspace_server

.PHONY: all run test clean
all: $(TARGET) astro_measure astro

astro_measure: tools/astro_measure.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra $< -o $@

# The terminal app (astro): a small client of the engine's /api/agent/* API.
astro: cli/astro.cpp cli/json.h cli/tui.inc cli/logo.h
	$(CXX) -std=c++20 -O2 -Wall -Wextra -pthread $(CURL_CFLAGS) $< -o $@ $(CURL_LIBS)

$(TARGET): workspace_server.cpp platform.h $(wildcard *.inc)
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $< -o $@ $(CURL_LIBS) -lcrypto -lutil

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) astro
	bash tests/smoke_cpp.sh
	node tests/agent_parse_test.js
	node tests/code_detect_test.js
	node library/build_library.js --check
	bash tests/library_e2e.sh
	bash tests/library_v2_e2e.sh
	bash tests/quality_test.sh
	bash tests/engine_e2e.sh
	node bench/run.js --self-test

clean:
	rm -f $(TARGET) astro_measure astro
