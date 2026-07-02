/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define EDITOR_VERSION "0.0.1"
#define TAB_WIDTH 8
#define QUIT_CONFIRMS 3

#define KEY_CTRL(k) ((k) & 0x1f)

enum SpecialKey {
  BACKSPACE = 127,
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

typedef struct TextRow {
  int len;
  int dlen;
  char *text;
  char *display;
} TextRow;

struct EditorState {
  int curX, curY;
  int rendX;
  int rowOff;
  int colOff;
  int winRows;
  int winCols;
  int numRows;
  TextRow *rows;
  int modified;
  char *fileName;
  char statusMsg[80];
  time_t statusMsgTime;
  struct termios origTermios;
};

struct EditorState Ed;

/*** prototypes ***/
void edSetStatusMessage(const char *fmt, ...);
void edRefreshScreen();
char *edPrompt(char *prompt);

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

/*** row operations ***/
int edRowCxToRx(TextRow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->text[j] == '\t')
      rx += (TAB_WIDTH - 1) - (rx % TAB_WIDTH);
    rx++;
  }
  return rx;
}

void edUpdateRow(TextRow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->len; j++)
    if (row->text[j] == '\t') tabs++;
  free(row->display);
  row->display = malloc(row->len + tabs * (TAB_WIDTH - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->len; j++) {
    if (row->text[j] == '\t') {
      row->display[idx++] = ' ';
      while (idx % TAB_WIDTH != 0) row->display[idx++] = ' ';
    } else {
      row->display[idx++] = row->text[j];
    }
  }
  row->display[idx] = '\0';
  row->dlen = idx;
}

void edInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > Ed.numRows) return;
  Ed.rows = realloc(Ed.rows, sizeof(TextRow) * (Ed.numRows + 1));
  memmove(&Ed.rows[at + 1], &Ed.rows[at], sizeof(TextRow) * (Ed.numRows - at));

  Ed.rows[at].len = len;
  Ed.rows[at].text = malloc(len + 1);
  memcpy(Ed.rows[at].text, s, len);
  Ed.rows[at].text[len] = '\0';

  Ed.rows[at].dlen = 0;
  Ed.rows[at].display = NULL;
  edUpdateRow(&Ed.rows[at]);

  Ed.numRows++;
  Ed.modified++;
}

void edFreeRow(TextRow *row) {
  free(row->display);
  free(row->text);
}

void edDelRow(int at) {
  if (at < 0 || at >= Ed.numRows) return;
  edFreeRow(&Ed.rows[at]);
  memmove(&Ed.rows[at], &Ed.rows[at + 1], sizeof(TextRow) * (Ed.numRows - at - 1));
  Ed.numRows--;
  Ed.modified++;
}

void edRowInsertChar(TextRow *row, int at, int c) {
  if (at < 0 || at > row->len) at = row->len;
  row->text = realloc(row->text, row->len + 2);
  memmove(&row->text[at + 1], &row->text[at], row->len - at + 1);
  row->len++;
  row->text[at] = c;
  edUpdateRow(row);
  Ed.modified++;
}

void edRowAppendString(TextRow *row, char *s, size_t len) {
  row->text = realloc(row->text, row->len + len + 1);
  memcpy(&row->text[row->len], s, len);
  row->len += len;
  row->text[row->len] = '\0';
  edUpdateRow(row);
  Ed.modified++;
}

void edRowDelChar(TextRow *row, int at) {
  if (at < 0 || at >= row->len) return;
  memmove(&row->text[at], &row->text[at + 1], row->len - at);
  row->len--;
  edUpdateRow(row);
  Ed.modified++;
}

/*** editor operations ***/
void edInsertChar(int c) {
  if (Ed.curY == Ed.numRows) {
    edInsertRow(Ed.numRows, "", 0);
  }
  edRowInsertChar(&Ed.rows[Ed.curY], Ed.curX, c);
  Ed.curX++;
}

void edInsertNewline() {
  if (Ed.curX == 0) {
    edInsertRow(Ed.curY, "", 0);
  } else {
    TextRow *row = &Ed.rows[Ed.curY];
    edInsertRow(Ed.curY + 1, &row->text[Ed.curX], row->len - Ed.curX);
    row = &Ed.rows[Ed.curY];
    row->len = Ed.curX;
    row->text[row->len] = '\0';
    edUpdateRow(row);
  }
  Ed.curY++;
  Ed.curX = 0;
}

void edDelChar() {
  if (Ed.curY == Ed.numRows) return;
  if (Ed.curX == 0 && Ed.curY == 0) return;
  TextRow *row = &Ed.rows[Ed.curY];
  if (Ed.curX > 0) {
    edRowDelChar(row, Ed.curX - 1);
    Ed.curX--;
  } else {
    Ed.curX = Ed.rows[Ed.curY - 1].len;
    edRowAppendString(&Ed.rows[Ed.curY - 1], row->text, row->len);
    edDelRow(Ed.curY);
    Ed.curY--;
  }
}

/*** file i/o ***/
char *edRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < Ed.numRows; j++)
    totlen += Ed.rows[j].len + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < Ed.numRows; j++) {
    memcpy(p, Ed.rows[j].text, Ed.rows[j].len);
    p += Ed.rows[j].len;
    *p = '\n';
    p++;
  }
  return buf;
}

void edOpen(char *filename) {
  free(Ed.fileName);
  Ed.fileName = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) panic("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    edInsertRow(Ed.numRows, line, linelen);
  }
  free(line);
  fclose(fp);
  Ed.modified = 0;
}

void edSave() {
  if (Ed.fileName == NULL) {
    Ed.fileName = edPrompt("Save as: %s (ESC to cancel)");
    if (Ed.fileName == NULL) {
      edSetStatusMessage("Save aborted");
      return;
    }
  }
  int len;
  char *buf = edRowsToString(&len);
  int fd = open(Ed.fileName, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        Ed.modified = 0;
        edSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  edSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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
void edScroll() {
  Ed.rendX = 0;
  if (Ed.curY < Ed.numRows) {
    Ed.rendX = edRowCxToRx(&Ed.rows[Ed.curY], Ed.curX);
  }
}

void edDrawRows(struct AppendBuf *ab) {
  int y;
  for (y = 0; y < Ed.winRows; y++) {
    int filerow = y + Ed.rowOff;
    if (filerow >= Ed.numRows) {
      if (Ed.numRows == 0 && y == Ed.winRows / 3) {
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
    } else {
      int len = Ed.rows[filerow].dlen - Ed.colOff;
      if (len < 0) len = 0;
      if (len > Ed.winCols) len = Ed.winCols;
      bufAppend(ab, &Ed.rows[filerow].display[Ed.colOff], len);
    }
    bufAppend(ab, "\x1b[K", 3);
    bufAppend(ab, "\r\n", 2);
  }
}

void edDrawStatusBar(struct AppendBuf *ab) {
  bufAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    Ed.fileName ? Ed.fileName : "[No Name]", Ed.numRows,
    Ed.modified ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    Ed.curY + 1, Ed.numRows);
  if (len > Ed.winCols) len = Ed.winCols;
  bufAppend(ab, status, len);
  while (len < Ed.winCols) {
    if (Ed.winCols - len == rlen) {
      bufAppend(ab, rstatus, rlen);
      break;
    } else {
      bufAppend(ab, " ", 1);
      len++;
    }
  }
  bufAppend(ab, "\x1b[m", 3);
  bufAppend(ab, "\r\n", 2);
}

void edDrawMessageBar(struct AppendBuf *ab) {
  bufAppend(ab, "\x1b[K", 3);
  int msglen = strlen(Ed.statusMsg);
  if (msglen > Ed.winCols) msglen = Ed.winCols;
  if (msglen && time(NULL) - Ed.statusMsgTime < 5)
    bufAppend(ab, Ed.statusMsg, msglen);
}

void edRefreshScreen() {
  edScroll();

  struct AppendBuf ab = APPENDBUF_INIT;
  bufAppend(&ab, "\x1b[?25l", 6);

  bufAppend(&ab, "\x1b[H", 3);
  edDrawRows(&ab);
  edDrawStatusBar(&ab);
  edDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (Ed.curY - Ed.rowOff) + 1,
                                            (Ed.rendX - Ed.colOff) + 1);
  bufAppend(&ab, buf, strlen(buf));
  bufAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.data, ab.len);
  bufFree(&ab);
}

void edSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(Ed.statusMsg, sizeof(Ed.statusMsg), fmt, ap);
  va_end(ap);
  Ed.statusMsgTime = time(NULL);
}

/*** input ***/
char *edPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    edSetStatusMessage(prompt, buf);
    edRefreshScreen();
    int c = edReadKey();
    if (c == DEL_KEY || c == KEY_CTRL('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      edSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        edSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void edMoveCursor(int key) {
  TextRow *row = (Ed.curY >= Ed.numRows) ? NULL : &Ed.rows[Ed.curY];
  switch (key) {
    case ARROW_LEFT:
      if (Ed.curX != 0) {
        Ed.curX--;
      } else if (Ed.curY > 0) {
        Ed.curY--;
        Ed.curX = Ed.rows[Ed.curY].len;
      }
      break;
    case ARROW_RIGHT:
      if (row && Ed.curX < row->len) {
        Ed.curX++;
      } else if (row && Ed.curX == row->len) {
        Ed.curY++;
        Ed.curX = 0;
      }
      break;
    case ARROW_UP:
      if (Ed.curY != 0) {
        Ed.curY--;
      }
      break;
    case ARROW_DOWN:
      if (Ed.curY < Ed.numRows) {
        Ed.curY++;
      }
      break;
  }
  row = (Ed.curY >= Ed.numRows) ? NULL : &Ed.rows[Ed.curY];
  int rowlen = row ? row->len : 0;
  if (Ed.curX > rowlen) {
    Ed.curX = rowlen;
  }
}

void edProcessKeypress() {
  static int quitTimes = QUIT_CONFIRMS;
  int c = edReadKey();
  switch (c) {
    case '\r':
      edInsertNewline();
      break;
    case KEY_CTRL('q'):
      if (Ed.modified && quitTimes > 0) {
        edSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quitTimes);
        quitTimes--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    case KEY_CTRL('s'):
      edSave();
      break;
    case HOME_KEY:
      Ed.curX = 0;
      break;
    case END_KEY:
      if (Ed.curY < Ed.numRows)
        Ed.curX = Ed.rows[Ed.curY].len;
      break;
    case BACKSPACE:
    case KEY_CTRL('h'):
    case DEL_KEY:
      if (c == DEL_KEY) edMoveCursor(ARROW_RIGHT);
      edDelChar();
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          Ed.curY = Ed.rowOff;
        } else if (c == PAGE_DOWN) {
          Ed.curY = Ed.rowOff + Ed.winRows - 1;
          if (Ed.curY > Ed.numRows) Ed.curY = Ed.numRows;
        }
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
    case KEY_CTRL('l'):
    case '\x1b':
      break;
    default:
      edInsertChar(c);
      break;
  }
  quitTimes = QUIT_CONFIRMS;
}

/*** init ***/
void edInit() {
  Ed.curX = 0;
  Ed.curY = 0;
  Ed.rendX = 0;
  Ed.rowOff = 0;
  Ed.colOff = 0;
  Ed.numRows = 0;
  Ed.rows = NULL;
  Ed.modified = 0;
  Ed.fileName = NULL;
  Ed.statusMsg[0] = '\0';
  Ed.statusMsgTime = 0;

  if (queryWindowSize(&Ed.winRows, &Ed.winCols) == -1) panic("queryWindowSize");
  Ed.winRows -= 2;
}

int main(int argc, char *argv[]) {
  enterRawMode();
  edInit();
  if (argc >= 2) {
    edOpen(argv[1]);
  }

  edSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

  while (1) {
    edRefreshScreen();
    edProcessKeypress();
  }
  return 0;
}
