CXX := g++
CPPFLAGS := -Isrc
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread -O2
LDFLAGS := -pthread
COMMON := src/model.cpp src/runtime.cpp src/cli.cpp
HEADERS := $(wildcard src/*.hpp)

.PHONY: all test clean sanitize
all: build/simulator

build:
	mkdir -p build

build/simulator: src/main.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) $(LDFLAGS) -o $@

build/tests: tests/tests.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/tests.cpp $(COMMON) $(LDFLAGS) -o $@

test: build/simulator build/tests
	./build/tests
	python3 tests/cli_test.py ./build/simulator

sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Wpedantic -pthread -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/tests.cpp $(COMMON) -o build/tests-sanitize
	./build/tests-sanitize

clean:
	rm -rf build
