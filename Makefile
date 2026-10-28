CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
TARGET := workspace_server

.PHONY: all run test clean
all: $(TARGET) astro_measure

astro_measure: tools/astro_measure.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra $< -o $@

$(TARGET): workspace_server.cpp platform.h $(wildcard *.inc)
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $< -o $@ $(CURL_LIBS) -lcrypto -lutil

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	bash tests/smoke_cpp.sh
	node tests/agent_parse_test.js
	node tests/code_detect_test.js
	node library/build_library.js --check
	bash tests/library_e2e.sh
	bash tests/library_v2_e2e.sh
	bash tests/quality_test.sh

clean:
	rm -f $(TARGET) astro_measure
