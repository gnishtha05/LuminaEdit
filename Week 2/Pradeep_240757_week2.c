/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define EDITOR_VERSION "0.0.1"

#define KEY_CTRL(k) ((k) & 0x1f)

enum SpecialKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/
struct EditorState {
  int curX, curY;
  int winRows;
  int winCols;
  struct termios origTermios;
};

struct EditorState Ed;

/*** terminal ***/
struct termios savedTermios;

void panic(const char *msg) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(msg);
  exit(1);
}

void restoreTerminal() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &Ed.origTermios) == -1)
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &savedTermios) == -1)
      panic("tcsetattr");
}

void enterRawMode() {
  if (tcgetattr(STDIN_FILENO, &Ed.origTermios) == -1) panic("tcgetattr");

  atexit(restoreTerminal);

  struct termios raw = Ed.origTermios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) panic("tcsetattr");
}

int edReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) panic("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int queryCursorPos(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int queryWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return queryCursorPos(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/
struct AppendBuf {
  char *data;
  int len;
};
#define APPENDBUF_INIT {NULL, 0}

void bufAppend(struct AppendBuf *ab, const char *s, int len) {
  char *newData = realloc(ab->data, ab->len + len);
  if (newData == NULL) return;
  memcpy(&newData[ab->len], s, len);
  ab->data = newData;
  ab->len += len;
}

void bufFree(struct AppendBuf *ab) {
  free(ab->data);
}

/*** output ***/
void edDrawRows(struct AppendBuf *ab) {
  int y;
  for (y = 0; y < Ed.winRows; y++) {
    if (y == Ed.winRows / 3) {
      char welcome[80];
      int welcomeLen = snprintf(welcome, sizeof(welcome),
        "Text editor -- version %s", EDITOR_VERSION);
      if (welcomeLen > Ed.winCols) welcomeLen = Ed.winCols;
      int padding = (Ed.winCols - welcomeLen) / 2;
      if (padding) {
        bufAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) bufAppend(ab, " ", 1);
      bufAppend(ab, welcome, welcomeLen);
    } else {
      bufAppend(ab, "~", 1);
    }

    bufAppend(ab, "\x1b[K", 3);
    if (y < Ed.winRows - 1) {
      bufAppend(ab, "\r\n", 2);
    }
  }
}

void edRefreshScreen() {
  struct AppendBuf ab = APPENDBUF_INIT;
  bufAppend(&ab, "\x1b[?25l", 6);

  bufAppend(&ab, "\x1b[H", 3);
  edDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Ed.curY + 1, Ed.curX + 1);
  bufAppend(&ab, buf, strlen(buf));
  bufAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.data, ab.len);
  bufFree(&ab);
}

/*** input ***/
void edMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (Ed.curX != 0) {
        Ed.curX--;
      }
      break;
    case ARROW_RIGHT:
      if (Ed.curX != Ed.winCols - 1) {
        Ed.curX++;
      }
      break;
    case ARROW_UP:
      if (Ed.curY != 0) {
        Ed.curY--;
      }
      break;
    case ARROW_DOWN:
      if (Ed.curY != Ed.winRows - 1) {
        Ed.curY++;
      }
      break;
  }
}

void edProcessKeypress() {
  int c = edReadKey();
  switch (c) {
    case KEY_CTRL('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    case HOME_KEY:
      Ed.curX = 0;
      break;
    case END_KEY:
      Ed.curX = Ed.winCols - 1;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = Ed.winRows;
        while (times--)
          edMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      edMoveCursor(c);
      break;
  }
}

/*** init ***/
void edInit() {
  Ed.curX = 0;
  Ed.curY = 0;
  if (queryWindowSize(&Ed.winRows, &Ed.winCols) == -1) panic("queryWindowSize");
}

int main() {
  enterRawMode();
  edInit();
  while (1) {
    edRefreshScreen();
    edProcessKeypress();
  }

  return 0;
}
