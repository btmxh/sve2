#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <log.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "sve2/utils/asprintf.h"
#include "sve2/utils/filewatch.h"
#include "sve2/utils/runtime.h"

typedef struct {
  char *path;
  int wd;
} filewatch_dir_t;

typedef struct {
  filewatch_dir_t *data;
  i32 len, cap;
} filewatch_dirs_t;

struct filewatch_t {
  int fd;
  filewatch_dirs_t dirs;
  char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
  i32 bufsize;
};

static filewatch_dir_t *find_dir(filewatch_t *fw, int wd, const char *name) {
  for (i32 i = 0; i < fw->dirs.len; ++i) {
    filewatch_dir_t *d = &fw->dirs.data[i];
    if (d->wd == wd || (name && strcmp(d->path, name) == 0)) {
      return d;
    }
  }

  return NULL;
}

static bool create_watch(filewatch_t *fw, const char *parent,
                         const char *child) {
  char *path =
      parent ? sve2_asprintf("%s/%s", parent, child) : sve2_strdup(child);

  int watch = inotify_add_watch(fw->fd, path,
                                IN_MODIFY | IN_MOVE | IN_CREATE | IN_DELETE);
  if (watch < 0) {
    free(path);
    return false;
  }

  if (fw->dirs.len >= fw->dirs.cap) {
    fw->dirs.cap = (fw->dirs.cap + 1) * 3 / 2;
    fw->dirs.data =
        realloc(fw->dirs.data, fw->dirs.cap * sizeof(filewatch_dir_t));
  }

  fw->dirs.data[fw->dirs.len++] = (filewatch_dir_t){
      .path = path,
      .wd = watch,
  };

  DIR *dir = opendir(path);
  if (!dir) {
    log_warn("unable to add watches for subdirectories of '%s'", path);
    return true;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 &&
        strcmp(entry->d_name, "..") != 0) {
      if (!create_watch(fw, path, entry->d_name)) {
        log_warn("unable to add watch for subdirectory '%s' of '%s'",
                 entry->d_name, path);
      }
    }
  }
  closedir(dir);

  log_trace("recursively add watch for directory '%s'", path);
  return true;
}

filewatch_t *filewatch_init(const char *monitor_dir) {
  int fd = inotify_init1(IN_NONBLOCK);
  if (fd == -1) {
    char msg[100];
    strerror_r(errno, msg, sizeof msg);
    log_error("unable to initialize inotify for filewatch: %s", msg);
    return NULL;
  }

  filewatch_t *fw = sve2_malloc(sizeof *fw);
  fw->fd = fd;
  fw->dirs.data = NULL;
  fw->dirs.len = 0;
  fw->dirs.cap = 0;
  fw->bufsize = 0;
  create_watch(fw, NULL, monitor_dir);

  return fw;
}

void filewatch_free(filewatch_t *fw) {
  for (i32 i = 0; i < fw->dirs.len; ++i) {
    free(fw->dirs.data[i].path);
    inotify_rm_watch(fw->fd, fw->dirs.data[i].wd);
  }

  free(fw->dirs.data);
  close(fw->fd);
  free(fw);
}

static void preprocess_event(filewatch_t *fw, struct inotify_event *ie) {
  if (ie->mask & (IN_CREATE | IN_MOVED_TO)) {
    if (ie->mask & IN_ISDIR) {
      if (!create_watch(fw, find_dir(fw, ie->wd, NULL)->path, ie->name)) {
        log_warn("unable to create watch for newly created directory: '%s'",
                 ie->name);
      }
    }
  }
}

bool filewatch_poll(filewatch_t *fw, filewatch_event *e) {
  if (fw->bufsize == 0) {
    i32 read_ret = read(fw->fd, fw->buf, sizeof fw->buf);
    if (read_ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
      }

      char msg[100];
      strerror_r(errno, msg, sizeof msg);
      log_error("unable to read inotify event for filewatch: %s", msg);
      return false;
    }

    if (read_ret == 0) {
      return false;
    }

    fw->bufsize = read_ret;
  }

  struct inotify_event *ie = (struct inotify_event *)fw->buf;
  const char *dir_path = find_dir(fw, ie->wd, NULL)->path;
  e->isdir = ie->mask & IN_ISDIR;
  e->name = sve2_asprintf("%s/%s", dir_path, ie->name);
  e->name_len = (i32)ie->len - 1;
  e->created = ie->mask & IN_CREATE;
  e->deleted = ie->mask & IN_DELETE;
  e->movedto = ie->mask & IN_MOVED_TO;
  e->movedfrom = ie->mask & IN_MOVED_FROM;
  e->modified = ie->mask & IN_MODIFY;
  preprocess_event(fw, ie);
  i32 in_event_size = sizeof(*ie) + ie->len;
  memmove(fw->buf, &fw->buf[in_event_size], fw->bufsize - in_event_size);
  fw->bufsize -= in_event_size;

  return true;
}

void filewatch_free_event(filewatch_t *fw, filewatch_event *e) {
  (void)fw;
  free(e->name);
}
