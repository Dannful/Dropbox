#include "../include/fs_sync.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1048

typedef struct {
  int socket_descriptor;
  int watch_descriptor;
} FsWatch;

FsWatch monitor;

void destroy() {
  inotify_rm_watch(monitor.socket_descriptor, monitor.watch_descriptor);
  close(monitor.socket_descriptor);
}

void *watcher(void *arg) {
  u_int8_t buffer[BUFFER_SIZE];
  int head = 0;
  int total_read;
  while ((total_read = read(monitor.socket_descriptor, buffer, BUFFER_SIZE)) >
         0) {
    head = 0;
    while (head < total_read) {
      struct inotify_event *event = (struct inotify_event *)(buffer + head);
      if (event->len == 0)
        continue;
      if (event->mask & IN_CREATE) {
        printf("createdddd\n");
      }
      head += sizeof(struct inotify_event) + event->len;
    }
  }
  pthread_exit(0);
}

SyncInitResult initialize(char path[]) {
  pthread_t thread_watcher;
  monitor.socket_descriptor = inotify_init();
  if (monitor.socket_descriptor < 0)
    return FILE_DESCRIPTOR_CREATE_ERROR;

  monitor.watch_descriptor = inotify_add_watch(
      monitor.socket_descriptor, path, IN_CREATE | IN_MODIFY | IN_DELETE);

  if (monitor.watch_descriptor < 0)
    return FAILED_TO_WATCH;

  pthread_create(&thread_watcher, NULL, watcher, NULL);

  return OK;
}
