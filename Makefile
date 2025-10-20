CC = gcc
CFLAGS = -g -Wall -pedantic
LDFLAGS = -lmicrohttpd

BIN = monitor
SRC = main.c monitor.c

$(BIN): $(SRC)
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm $(BIN)

