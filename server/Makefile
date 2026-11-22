CC ?= gcc
CFLAGS := -std=c17 -Wall -Wextra -Iinclude
SAN_FLAGS := -g -fsanitize=address,undefined

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR := build

DICT_SRCS := $(SRC_DIR)/dictionary.c
SNAP_SRCS := $(SRC_DIR)/snapshot.c $(DICT_SRCS)

TEST_DICT_BIN := $(BUILD_DIR)/test_dict
TEST_SNAP_BIN := $(BUILD_DIR)/test_snapshot

.PHONY: all test san clean

all: $(TEST_DICT_BIN) $(TEST_SNAP_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_DICT_BIN): $(TEST_DIR)/test_dict.c $(DICT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_SNAP_BIN): $(TEST_DIR)/test_snapshot.c $(SNAP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

test: all
	./$(TEST_DICT_BIN)
	@echo "----------------------------------------"
	./$(TEST_SNAP_BIN)

san: CFLAGS += $(SAN_FLAGS)
san: clean test

clean:
	rm -rf $(BUILD_DIR)