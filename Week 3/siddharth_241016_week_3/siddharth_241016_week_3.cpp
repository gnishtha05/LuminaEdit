#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

typedef struct erw {
    int sz;
    char *c;
} erw;

struct cfg {
    int cx, cy;
    int ro, co;
    int nr;
    int drt;
    char *fn;
    erw *rw;
    struct termios otm;
} E;

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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.otm);
}

void erm() {
    tcgetattr(STDIN_FILENO, &E.otm);
    atexit(drm);
    struct termios rw = E.otm;
    rw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    rw.c_oflag &= ~(OPOST);
    rw.c_cflag |= (CS8);
    rw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    rw.c_cc[VMIN] = 0;
    rw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &rw);
}

void irw(int at, char *s, size_t l) {
    if (at < 0 || at > E.nr) return;
    E.rw = realloc(E.rw, sizeof(erw) * (E.nr + 1));
    memmove(&E.rw[at + 1], &E.rw[at], sizeof(erw) * (E.nr - at));
    E.rw[at].sz = l;
    E.rw[at].c = malloc(l + 1);
    memcpy(E.rw[at].c, s, l);
    E.rw[at].c[l] = '\0';
    E.nr++;
    E.drt++;
}

void opn(char *fn) {
    free(E.fn);
    E.fn = strdup(fn);
    FILE *fp = fopen(fn, "r");
    if (!fp) return;
    char *ln = NULL;
    size_t cp = 0;
    ssize_t ln_l;
    while ((ln_l = getline(&ln, &cp, fp)) != -1) {
        while (ln_l > 0 && (ln[ln_l - 1] == '\n' || ln[ln_l - 1] == '\r')) ln_l--;
        irw(E.nr, ln, ln_l);
    }
    free(ln);
    fclose(fp);
    E.drt = 0;
}

void sv() {
    if (E.fn == NULL) return;
    int l = 0;
    for (int i = 0; i < E.nr; i++) l += E.rw[i].sz + 1;
    char *b = malloc(l);
    char *p = b;
    for (int i = 0; i < E.nr; i++) {
        memcpy(p, E.rw[i].c, E.rw[i].sz);
        p += E.rw[i].sz;
        *p = '\n';
        p++;
    }
    int fd = open(E.fn, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        ftruncate(fd, l);
        write(fd, b, l);
        close(fd);
        E.drt = 0;
    }
    free(b);
}

void edr(struct abf *a) {
    for (int y = 0; y < E.ro; y++) {
        if (y < E.nr) {
            int l = E.rw[y].sz;
            if (l > E.co) l = E.co;
            abp(a, E.rw[y].c, l);
        } else {
            abp(a, "~", 1);
        }
        abp(a, "\x1b[K", 3);
        abp(a, "\r\n", 2);
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "\x1b[7m%s %s\x1b[m", E.fn ? E.fn : "[No Name]", E.drt ? "(mod)" : "");
    abp(a, buf, strlen(buf));
}

void ers() {
    struct abf a = ABUF_INIT;
    abp(&a, "\x1b[?25l", 6);
    abp(&a, "\x1b[H", 3);
    edr(&a);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abp(&a, buf, strlen(buf));
    abp(&a, "\x1b[?25h", 6);
    write(STDOUT_FILENO, a.b, a.l);
    abf(&a);
}

void slv() {
    erm();
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    E.ro = ws.ws_row - 1;
    E.co = ws.ws_col;
    E.nr = 0;
    E.cx = 0;
    E.cy = 0;
    E.drt = 0;
    E.fn = NULL;
    E.rw = NULL;
    
    opn("test.txt");

    while (1) {
        ers();
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        if (c == 'q') {
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        } else if (c == 's') {
            sv();
        }
    }
}

int main() {
    slv();
    return 0;
}