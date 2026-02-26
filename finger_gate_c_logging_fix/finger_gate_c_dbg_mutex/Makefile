CC=gcc
CFLAGS=-O2 -Wall -Wextra -I./
LDFLAGS=-lpthread

SRC=src/main.c src/uart.c src/waveshare_fp.c src/user_db.c src/gate_stub.c
OUT=finger_gate

all:
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT)
