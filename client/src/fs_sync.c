#include "../include/fs_sync.h"
#include "../include/connection.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1048

typedef struct {
  int socket_descriptor;
  int watch_descriptor;
} FsWatch;

FsWatch monitor;
extern Map *path_descriptors;
extern sem_t pooling_semaphore;
extern Map *file_timestamps;

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
      char in_dir_path[256];
      sprintf(in_dir_path, "./syncdir/%s", event->name);
      if (event->mask & IN_CREATE || event->mask & IN_MOVE ||
          event->mask & IN_MODIFY) {
        struct stat path_stat;
        stat(in_dir_path, &path_stat);
        if (!S_ISREG(path_stat.st_mode)) {
          head += sizeof(struct inotify_event) + event->len;
          continue;
        }
        sem_wait(&pooling_semaphore);
        if (hash_has(file_timestamps, event->name) &&
            *((unsigned long *)hash_get(file_timestamps, event->name)) ==
                path_stat.st_mtim.tv_sec) {
          head += sizeof(struct inotify_event) + event->len;
          sem_post(&pooling_semaphore);
          continue;
        }
        printf("Changes detected: %s.\n", event->name);
        hash_set(file_timestamps, event->name,
                 &(unsigned long){path_stat.st_mtim.tv_sec});
        if (access(in_dir_path, F_OK) == 0)
          send_upload_message(in_dir_path);
        sem_post(&pooling_semaphore);
      }
      if (event->mask & IN_DELETE) {
        sem_wait(&pooling_semaphore);
        if (hash_has(path_descriptors, event->name)) {
          head += sizeof(struct inotify_event) + event->len;
          continue;
        }
        printf("Delete file: %s.\n", event->name);
        send_delete_message(event->name);
        sem_post(&pooling_semaphore);
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

  monitor.watch_descriptor =
      inotify_add_watch(monitor.socket_descriptor, path,
                        IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVE);

  if (monitor.watch_descriptor < 0)
    return FAILED_TO_WATCH;

  pthread_create(&thread_watcher, NULL, watcher, NULL);

  return OK;
}
