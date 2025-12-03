CC = gcc
CFLAGS = -Wall -Wextra -pthread -lrt -O2
SRC = src/main_v2.c
BIN = concurrent-http-server

.PHONY: all clean run

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o bin/$(BIN) $(SRC)

clean:
	rm -rf bin

run: all
	./bin/$(BIN) server.conf
