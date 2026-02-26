#include "gate.h"
#include <stdio.h>
#include <unistd.h>

int gate_init(void) {
    printf("GATE: simulation mode (no GPIO)\n");
    fflush(stdout);
    return 0;
}

void gate_open_ms(int ms) {
    printf("GATE: UNLOCKED for %d ms\n", ms);
    fflush(stdout);
    usleep(ms * 1000);
    printf("GATE: LOCKED\n");
    fflush(stdout);
}

void gate_close(void) {
    printf("GATE: LOCKED\n");
    fflush(stdout);
}

void gate_cleanup(void) { }
