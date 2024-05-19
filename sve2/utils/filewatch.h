#pragma once

#include "sve2/utils/types.h"

// simple filewatch API
typedef struct filewatch_t filewatch_t;

typedef struct {
  char *name;
  i32 name_len;
  bool created : 1;
  bool deleted : 1;
  bool isdir : 1;
  bool movedfrom : 1;
  bool movedto : 1;
  bool modified : 1;
} filewatch_event;

filewatch_t *filewatch_init(const char *monitor_dir);
void filewatch_free(filewatch_t *fw);

bool filewatch_poll(filewatch_t *fw, filewatch_event *e);
void filewatch_free_event(filewatch_t *fw, filewatch_event *e);
