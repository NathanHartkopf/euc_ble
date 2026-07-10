#include "serial_feed.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

namespace {

speed_t baudToFlag(int baud) {
  switch (baud) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
    default:
      return B115200;
  }
}

}  // namespace

bool serialFeedOpen(SerialFeed &feed, const char *port, int baud) {
  serialFeedClose(feed);

  const int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    fprintf(stderr, "Serial: failed to open %s (%s)\n", port, strerror(errno));
    return false;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0) {
    fprintf(stderr, "Serial: tcgetattr failed (%s)\n", strerror(errno));
    close(fd);
    return false;
  }

  cfmakeraw(&tty);
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag |= CREAD | CLOCAL;
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  const speed_t speed = baudToFlag(baud);
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    fprintf(stderr, "Serial: tcsetattr failed (%s)\n", strerror(errno));
    close(fd);
    return false;
  }

  feed.fd = fd;
  feed.line_len = 0;
  feed.last_rx_ms = 0;
  fprintf(stderr, "Serial: opened %s @ %d baud\n", port, baud);
  return true;
}

void serialFeedClose(SerialFeed &feed) {
  if (feed.fd >= 0) {
    close(feed.fd);
    feed.fd = -1;
  }
  feed.line_len = 0;
}

bool serialFeedIsOpen(const SerialFeed &feed) { return feed.fd >= 0; }

void serialFeedPoll(SerialFeed &feed, uint32_t now_ms,
                    void (*on_line)(const char *line, void *user), void *user) {
  if (!serialFeedIsOpen(feed) || !on_line) {
    return;
  }

  char chunk[256];
  while (true) {
    const ssize_t n = read(feed.fd, chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      fprintf(stderr, "Serial: read error (%s)\n", strerror(errno));
      serialFeedClose(feed);
      return;
    }
    if (n == 0) {
      break;
    }

    feed.last_rx_ms = now_ms;

    for (ssize_t i = 0; i < n; i++) {
      const char c = chunk[i];
      if (c == '\r') {
        continue;
      }

      if (c == '\n') {
        feed.line_buf[feed.line_len] = '\0';
        if (feed.line_len > 0) {
          on_line(feed.line_buf, user);
        }
        feed.line_len = 0;
        continue;
      }

      if (feed.line_len + 1 >= sizeof(feed.line_buf)) {
        feed.line_len = 0;
        continue;
      }

      feed.line_buf[feed.line_len++] = c;
    }
  }
}
