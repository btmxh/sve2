#include "cmd_queue.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <log.h>
#include <stb/stb_ds.h>
#include <sys/select.h>
#include <unistd.h>

void cmd_queue_init(cmd_queue_t *q) {
  char *cmd_file = getenv("CMD_FILE");
  if (cmd_file) {
    q->fd = open(cmd_file, O_RDONLY | O_NONBLOCK);
  } else {
    q->fd = -1;
  }
  q->buffer = NULL;
}

void cmd_queue_free(cmd_queue_t *q) {
  stbds_arrfree(q->buffer);
  close(q->fd);
}

i32 cmd_queue_get(cmd_queue_t *q, char *line[static 1]) {
  if (q->fd >= 0) {
    char c;
    int err;
    while ((err = read(q->fd, &c, 1)) == 1) {
      if (c == '\n') {
        stbds_arrput(q->buffer, '\0');
        *line = q->buffer;
        return stbds_arrlen(q->buffer);
      } else {
        stbds_arrput(q->buffer, c);
      }
    }

    if (err == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      log_warn("error reading from command queue: %s", strerror(errno));
    }

    if (err == 0 && stbds_arrlen(q->buffer) > 0) {
      if (stbds_arrlast(q->buffer) == '\n') {
        stbds_arrlast(q->buffer) = '\0';
      } else {
        stbds_arrput(q->buffer, '\0');
      }

      *line = q->buffer;
      return stbds_arrlen(q->buffer);
    }
  }

  *line = NULL;
  return 0;
}

void cmd_queue_unget(cmd_queue_t *q, char *line) {
  assert(line == q->buffer);
  stbds_arrfree(q->buffer);
}
