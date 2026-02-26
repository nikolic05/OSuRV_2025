#pragma once
#include <stdint.h>
#include <stddef.h>

int uart_open(const char* dev, int baudrate);
void uart_close(int fd);
int uart_write_all(int fd, const uint8_t* buf, size_t n);
int uart_read_all(int fd, uint8_t* buf, size_t n, int timeout_ms);
