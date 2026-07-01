#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

struct termios otm;

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

void slv() {
    erm();
    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && c != EAGAIN) exit(1);
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }
}

int main() {
    slv();
    return 0;
}