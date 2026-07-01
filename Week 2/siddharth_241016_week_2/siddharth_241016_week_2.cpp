#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct termios otm;
int row, col, cx, cy;

struct abf {
    char *b;
    int l;
};

#define ABUF_INIT {NULL, 0}

void abp(struct abf *a, const char *s, int l) {
    char *n = realloc(a->b, a->l + l);
    if (!n) return;
    memcpy(&n[a->l], s, l);
    a->b = n;
    a->l += l;
}

void abf(struct abf *a) {
    free(a->b);
}

void drm() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &otm);
}

void erm() {
    tcgetattr(STDIN_FILENO, &otm);
    atexit(drm);
    struct termios rw = otm;
    rw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    rw.c_oflag &= ~(OPOST);
    rw.c_cflag |= (CS8);
    rw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    rw.c_cc[VMIN] = 0;
    rw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &rw);
}

int gwz(int *r, int *c) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return -1;
    *c = ws.ws_col;
    *r = ws.ws_row;
    return 0;
}

void edr(struct abf *a) {
    for (int y = 0; y < row; y++) {
        abp(a, "~", 1);
        abp(a, "\x1b[K", 3);
        if (y < row - 1) abp(a, "\r\n", 2);
    }
}

void ers() {
    struct abf a = ABUF_INIT;
    abp(&a, "\x1b[?25l", 6);
    abp(&a, "\x1b[H", 3);
    edr(&a);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + 1, cx + 1);
    abp(&a, buf, strlen(buf));
    abp(&a, "\x1b[?25h", 6);
    write(STDOUT_FILENO, a.b, a.l);
    abf(&a);
}

void slv() {
    erm();
    gwz(&row, &col);
    cx = 0;
    cy = 0;
    while (1) {
        ers();
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        if (c == 'q') {
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        }
        if (c == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': if (cy != 0) cy--; break;
                    case 'B': if (cy != row - 1) cy++; break;
                    case 'C': if (cx != col - 1) cx++; break;
                    case 'D': if (cx != 0) cx--; break;
                }
            }
        }
    }
}

int main() {
    slv();
    return 0;
}