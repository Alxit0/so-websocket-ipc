CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread -lrt

# Source files
SRC_DIR = src
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/logger.c \
       $(SRC_DIR)/stats.c \
       $(SRC_DIR)/http.c \
       $(SRC_DIR)/thread_pool.c \
       $(SRC_DIR)/server.c

# Object files
OBJ_DIR = obj
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Binary
BIN_DIR = bin
BIN = $(BIN_DIR)/concurrent-http-server

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)

run: all
	./$(BIN) server.conf
