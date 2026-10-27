CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
TARGET := workspace_server

.PHONY: all run test clean
all: $(TARGET) astro_measure

astro_measure: tools/astro_measure.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra $< -o $@

$(TARGET): workspace_server.cpp platform.h astro_ext.inc workspace_code.inc workspace_ide.inc workspace_intel.inc workspace_agent.inc
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $< -o $@ $(CURL_LIBS) -lcrypto -lutil

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	bash tests/smoke_cpp.sh

clean:
	rm -f $(TARGET) astro_measure
