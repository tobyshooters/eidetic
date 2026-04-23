CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Ideps
LDFLAGS = -lSDL2 -lm

SRC = src
BUILD = build

all: directories $(BUILD)/fliptable

$(BUILD)/fliptable: $(BUILD)/main.o $(BUILD)/db.o $(BUILD)/glyph.o $(BUILD)/eval.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

directories:
	mkdir -p $(BUILD)

run: $(BUILD)/fliptable
	rlwrap ./$(BUILD)/fliptable

debug: CFLAGS += -g -DDEBUG -O0 -ggdb3
debug: directories $(BUILD)/fliptable

format:
	clang-format --style=Mozilla -i $(SRC)/main.c $(SRC)/db.c $(SRC)/glyph.c $(SRC)/eval.c

clean:
	rm -rf $(BUILD)

.PHONY: all directories run debug format clean
