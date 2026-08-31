CC ?= gcc
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Isrc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS ?=
THREAD_FLAGS := -pthread
LDLIBS := -lncurses

TARGET := os_sim
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
SELECTION_TEST := build/selection_test

.PHONY: all clean debug test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(THREAD_FLAGS) $(LDLIBS) -o $@

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) -c $< -o $@

build:
	mkdir -p build

debug: CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O0 -g3
debug: clean all

$(SELECTION_TEST): tests/selection_test.c src/proc_list.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $^ -o $@

test: all $(SELECTION_TEST)
	./$(SELECTION_TEST)
	sh tests/smoke.sh

clean:
	rm -rf build $(TARGET)
