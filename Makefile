# Default build is the sanitized debug build, because that is the one tests run
# against: every test run doubles as a memory-safety and UB check.

CC := gcc

# _POSIX_C_SOURCE exposes getline(), fsync() and friends while staying on C11.
CSTD := -std=c11 -D_POSIX_C_SOURCE=200809L
WARN := -Wall -Wextra -Werror
DEPFLAGS := -MMD -MP

# -fno-sanitize-recover makes UBSan abort instead of printing and carrying on,
# so undefined behaviour actually fails a test.
DEBUG_FLAGS := -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
RELEASE_FLAGS := -O2 -DNDEBUG

SRC := $(wildcard src/*.c)
DEBUG_DIR := build/debug
RELEASE_DIR := build/release
DEBUG_OBJ := $(SRC:src/%.c=$(DEBUG_DIR)/%.o)
RELEASE_OBJ := $(SRC:src/%.c=$(RELEASE_DIR)/%.o)
DEBUG_BIN := $(DEBUG_DIR)/btree_db
RELEASE_BIN := $(RELEASE_DIR)/btree_db

# Prefer the project venv (where pytest is installed) over the system Python.
PYTHON := $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)

.PHONY: all debug release test clean

all: debug

debug: $(DEBUG_BIN)

release: $(RELEASE_BIN)

$(DEBUG_BIN): $(DEBUG_OBJ)
	$(CC) $(DEBUG_FLAGS) $^ -o $@

$(RELEASE_BIN): $(RELEASE_OBJ)
	$(CC) $(RELEASE_FLAGS) $^ -o $@

$(DEBUG_DIR)/%.o: src/%.c | $(DEBUG_DIR)
	$(CC) $(CSTD) $(WARN) $(DEPFLAGS) $(DEBUG_FLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: src/%.c | $(RELEASE_DIR)
	$(CC) $(CSTD) $(WARN) $(DEPFLAGS) $(RELEASE_FLAGS) -c $< -o $@

$(DEBUG_DIR) $(RELEASE_DIR):
	mkdir -p $@

test: $(DEBUG_BIN)
	$(PYTHON) -m pytest -q tests

clean:
	rm -rf build

-include $(DEBUG_OBJ:.o=.d) $(RELEASE_OBJ:.o=.d)
