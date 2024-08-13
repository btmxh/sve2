#pragma once

#include "sve2/utils/types.h"

typedef struct {
  int fd;
  char *buffer;
} cmd_queue_t;

void cmd_queue_init(cmd_queue_t *q);
void cmd_queue_free(cmd_queue_t *q);
i32 cmd_queue_get(cmd_queue_t *q, char *line[static 1]);
void cmd_queue_unget(cmd_queue_t *q, char *line);
