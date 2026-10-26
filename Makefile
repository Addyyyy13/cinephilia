CC = clang
CFLAGS = -std=gnu11 -Wall -Wextra -Wpedantic -I include
LDFLAGS = -lm

SRC_DIR = src
INC_DIR = include
BIN = cinephilia

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/film.c \
       $(SRC_DIR)/storage.c \
       $(SRC_DIR)/ui.c \
       $(SRC_DIR)/search.c \
       $(SRC_DIR)/journal.c \
       $(SRC_DIR)/discovery.c \
       $(SRC_DIR)/stats.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

run: $(BIN)
	./$(BIN)

test: $(BIN)
	bash tests/test_cinephilia.sh
