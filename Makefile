CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -O0 -g

SRC_DIR   := src
BUILD_DIR := build

TEST_LEXER_BIN := $(BUILD_DIR)/test_lexer
TEST_LEXER_SRC := $(SRC_DIR)/test_lexer.c
TEST_LEXER_DEPS := $(SRC_DIR)/lexer.c $(SRC_DIR)/lexer.h

.PHONY: all clean

all: $(TEST_LEXER_BIN)

# Unity build: test_lexer.c #includes lexer.c — a single translation unit.
$(TEST_LEXER_BIN): $(TEST_LEXER_SRC) $(TEST_LEXER_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_LEXER_SRC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
