#ifndef FS_SYNC_H
#define FS_SYNC_H

typedef enum {
  FILE_DESCRIPTOR_CREATE_ERROR = -2,
  FAILED_TO_WATCH = -1,
  OK = 0
} SyncInitResult;

void destroy();

SyncInitResult initialize(char path[]);

#endif
