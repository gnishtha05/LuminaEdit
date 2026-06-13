#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>







struct termios orig_termios;

void die(const char *s) {
    perror(s);
    exit(1);
}




void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = orig_termios;

    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

#define CTRL_KEY(k) ((k) & 0x1f)





int main() {
    enableRawMode();

    char c;

    while (1) {
        if (read(STDIN_FILENO, &c, 1) == 1) {

            if (c == CTRL_KEY('q'))
                break;

            printf("%d\r\n", c);
        }
    }

    return 0;
}