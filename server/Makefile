
CC ?= gcc
CFLAGS := -std=c17 -Wall -Wextra -Iinclude -pthread
SAN_FLAGS := -g -fsanitize=address,undefined

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR := build

DICT_SRCS := $(SRC_DIR)/dictionary.c
SNAP_SRCS := $(SRC_DIR)/snapshot.c $(DICT_SRCS)
CMD_SRCS := $(SRC_DIR)/commands.c
HANDLER_SRCS := $(SRC_DIR)/handlers.c
EXPIRE_SRCS := $(SRC_DIR)/active_expire.c
SNAPSHOT_WORKER_SRCS := $(SRC_DIR)/background_snapshot.c

SERVER_SRCS := $(SRC_DIR)/server.c \
               $(CMD_SRCS) \
               $(HANDLER_SRCS) \
               $(SNAP_SRCS) \
               $(EXPIRE_SRCS) \
               $(SNAPSHOT_WORKER_SRCS)
			   
TEST_DICT_BIN := $(BUILD_DIR)/test_dict
TEST_SNAP_BIN := $(BUILD_DIR)/test_snapshot
TEST_HANDLER_BIN := $(BUILD_DIR)/test_handlers
SERVER_BIN := $(BUILD_DIR)/kedis-server

.PHONY: all test san run client clean

all: $(TEST_DICT_BIN) $(TEST_SNAP_BIN) $(TEST_HANDLER_BIN) $(SERVER_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_DICT_BIN): $(TEST_DIR)/test_dict.c $(DICT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_SNAP_BIN): $(TEST_DIR)/test_snapshot.c $(SNAP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_HANDLER_BIN): $(TEST_DIR)/test_handlers.c $(HANDLER_SRCS) $(CMD_SRCS) $(DICT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(SERVER_BIN): $(SERVER_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_DICT_BIN) $(TEST_SNAP_BIN) $(TEST_HANDLER_BIN)
	./$(TEST_DICT_BIN)
	@echo "----------------------------------------"
	./$(TEST_SNAP_BIN)
	@echo "----------------------------------------"
	./$(TEST_HANDLER_BIN)

san: CFLAGS += $(SAN_FLAGS)
san: clean all test

run: $(SERVER_BIN)
	./$(SERVER_BIN)

client:
	nc 127.0.0.1 7379

clean:
	rm -rf $(BUILD_DIR)
