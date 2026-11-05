CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
TARGET := workspace_server
# the built-in pack tools are their own translation units, one per group (keeps the compiler's memory in bounds;
# each is rebuilt only when the pack tools change). packtools/packtools.inc lists the groups.
PACKTOOLS_SRC := $(wildcard packtools/packtools*.cpp)
PACKTOOLS_OBJ := $(patsubst packtools/%.cpp,build/%.o,$(PACKTOOLS_SRC))

.PHONY: all run test clean
all: $(TARGET) astro_measure astro

astro_measure: tools/astro_measure.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra $< -o $@

# The terminal app (astro): a small client of the engine's /api/agent/* API.
astro: cli/astro.cpp cli/json.h cli/tui.inc cli/logo.h
	$(CXX) -std=c++20 -O2 -Wall -Wextra -pthread $(CURL_CFLAGS) $< -o $@ $(CURL_LIBS)

build/%.o: packtools/%.cpp $(wildcard packtools/*.inc packtools/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): workspace_server.cpp platform.h $(wildcard *.inc) packtools/packtools_api.h $(PACKTOOLS_OBJ)
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) $< $(PACKTOOLS_OBJ) -o $@ $(CURL_LIBS) -lcrypto -lutil -lz

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) astro
	bash tests/smoke_cpp.sh
	node tests/agent_parse_test.js
	node tests/agent_audit_test.js
	node tests/risky_test.js
	node tests/exthost_test.js
	node tests/code_detect_test.js
	node library/build_library.js --check
	bash packtools/tests/run_all.sh
	bash tests/packtools_e2e.sh
	bash tests/library_e2e.sh
	bash tests/library_v2_e2e.sh
	bash tests/library_v3_e2e.sh
	bash tests/local_engines_e2e.sh
	bash tests/indexer_sources_e2e.sh
	bash tests/quality_test.sh
	bash tests/engine_e2e.sh
	bash tests/macos_macros.sh
	bash tests/i18n_e2e.sh
	bash tests/agent_slash_e2e.sh
	bash tests/seed_tools_e2e.sh
	node bench/run.js --self-test

clean:
	rm -f $(TARGET) astro_measure astro $(PACKTOOLS_OBJ)
