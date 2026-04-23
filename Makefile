CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -O0 -g

CODE_DIR  := code
BUILD_DIR := build

JIVE_BIN  := $(BUILD_DIR)/jive
JIVE_SRC  := $(CODE_DIR)/main.c
JIVE_DEPS := $(CODE_DIR)/string.c \
             $(CODE_DIR)/lexer.c $(CODE_DIR)/lexer.h \
             $(CODE_DIR)/parser.c $(CODE_DIR)/codegen.c

TEST_LEXER_BIN  := $(BUILD_DIR)/test_lexer
TEST_LEXER_SRC  := $(CODE_DIR)/test_lexer.c
TEST_LEXER_DEPS := $(CODE_DIR)/string.c $(CODE_DIR)/lexer.c $(CODE_DIR)/lexer.h

.PHONY: all clean

all: $(JIVE_BIN) $(TEST_LEXER_BIN)

# Unity build: main.c #includes string.c, lexer.c, parser.c, codegen.c — one TU.
$(JIVE_BIN): $(JIVE_SRC) $(JIVE_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(JIVE_SRC)

# Unity build: test_lexer.c #includes string.c, lexer.c — a single translation unit.
$(TEST_LEXER_BIN): $(TEST_LEXER_SRC) $(TEST_LEXER_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_LEXER_SRC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
