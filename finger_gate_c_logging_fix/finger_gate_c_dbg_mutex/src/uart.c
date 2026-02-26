#include "uart.h"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <string.h>
#include <errno.h>

static speed_t baud_to_termios(int baud) {
    return B115200;
}

//otvaranje serijskog uredjaja
int uart_open(const char* dev, int baudrate) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    struct termios tty;
    memset(&tty, 0, sizeof tty);

    //trenutne postavke serijskog porta -> tty
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }

    cfsetospeed(&tty, baud_to_termios(baudrate));//izlazna brzina
    cfsetispeed(&tty, baud_to_termios(baudrate));//ulazna brzina

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; //postavljanje velicine karaktera na 8 bita
    tty.c_cflag |= (CLOCAL | CREAD);//ukljucenje receiver-a(citanje)
    tty.c_cflag &= ~(PARENB | PARODD);//iskljucenje pariteta
    tty.c_cflag &= ~CSTOPB;//1 stop bit
    tty.c_cflag &= ~CRTSCTS;//iskljucenje flow control-a

    //Ciscenje input/local/output processing:
    tty.c_iflag = 0;
    tty.c_lflag = 0;
    tty.c_oflag = 0;

    //read neblokirajuce, moze da vrati 0
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    //postavljanje atributa
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }
    return fd;
}

//zatvaranje uart-a
void uart_close(int fd) { if (fd >= 0) close(fd); }

//pisanje na serijski port
int uart_write_all(int fd, const uint8_t* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}
//citanje sa serijskog port-a
int uart_read_all(int fd, uint8_t* buf, size_t n, int timeout_ms) {
    size_t off = 0;
    while (off < n) {
        //set za select
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;//sekunde
        tv.tv_usec = (timeout_ms % 1000) * 1000;//milisekunde

        int rv = select(fd + 1, &set, NULL, NULL, &tv);
        if (rv == 0) return -2; // timeout
        if (rv < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        ssize_t r = read(fd, buf + off, n - off);
        if (r <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}
