#pragma once

#include <stddef.h>
#include <stdint.h>

struct SerialFeed {
  int fd = -1;
  char line_buf[512];
  size_t line_len = 0;
  uint32_t last_rx_ms = 0;
};

bool serialFeedOpen(SerialFeed &feed, const char *port, int baud);
void serialFeedClose(SerialFeed &feed);
bool serialFeedIsOpen(const SerialFeed &feed);
void serialFeedPoll(SerialFeed &feed, uint32_t now_ms,
                    void (*on_line)(const char *line, void *user), void *user);
