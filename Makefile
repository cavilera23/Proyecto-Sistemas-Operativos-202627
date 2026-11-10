CXX := g++
CPPFLAGS := -Isrc
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -Werror -pthread -O2
LDFLAGS := -pthread
COMMON := src/model.cpp src/runtime.cpp src/cli.cpp src/gui.cpp
HEADERS := $(wildcard src/*.hpp)

# GUI=auto enables the bonus when Qt Widgets is installed; GUI=0 is console-only.
GUI ?= auto
QT_PACKAGE := $(shell pkg-config --exists Qt6Widgets 2>/dev/null && echo Qt6Widgets || (pkg-config --exists Qt5Widgets 2>/dev/null && echo Qt5Widgets))
ifneq ($(GUI),0)
ifneq ($(QT_PACKAGE),)
CPPFLAGS += -DSIM_WITH_QT $(shell pkg-config --cflags $(QT_PACKAGE))
CXXFLAGS += -fPIC
LDFLAGS += $(shell pkg-config --libs $(QT_PACKAGE))
else ifeq ($(GUI),1)
$(error Qt Widgets no esta instalado o pkg-config no lo encuentra)
endif
endif

.PHONY: all test clean sanitize gui-test FORCE
all: build/simulator

build:
	mkdir -p build

# Rebuild when switching GUI=0/1 or compiler flags without requiring make clean.
FORCE:

build/flags: FORCE | build
	@printf '%s\n' '$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS)' > build/flags.tmp
	@cmp -s build/flags.tmp $@ || cp build/flags.tmp $@

build/simulator: src/main.cpp $(COMMON) $(HEADERS) Makefile build/flags | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) $(LDFLAGS) -o $@

build/tests: tests/tests.cpp $(COMMON) $(HEADERS) Makefile build/flags | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/tests.cpp $(COMMON) $(LDFLAGS) -o $@

test: build/simulator build/tests
	./build/tests
	python3 tests/cli_test.py ./build/simulator

sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Wpedantic -pthread -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/tests.cpp $(COMMON) -fPIC $(LDFLAGS) -o build/tests-sanitize
	./build/tests-sanitize

clean:
	rm -rf build

ifneq ($(filter -DSIM_WITH_QT,$(CPPFLAGS)),)
build/gui-tests: tests/gui_test.cpp $(COMMON) $(HEADERS) Makefile build/flags | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/gui_test.cpp $(COMMON) $(LDFLAGS) -o $@

gui-test: build/simulator build/gui-tests
	QT_QPA_PLATFORM=offscreen ./build/gui-tests
	python3 tests/gui_cli_test.py ./build/simulator
else
gui-test:
	@echo 'GUI deshabilitada. Instale Qt Widgets y ejecute make GUI=1 gui-test.'
	@exit 1
endif
