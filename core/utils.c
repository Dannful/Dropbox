#include "./utils.h"
#include "./packet.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

void generate_file_list_string(char dir_path[], char buffer[],
                               size_t buffer_size) {
  snprintf(buffer, buffer_size, "%-25s%-25s%-25s%-25s\n", "File Name",
           "Modification Time", "Access Time", "Creation Time");

  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    printf("Error opening directory: %s\n", dir_path);
    return;
  }

  size_t current_length = strlen(buffer);
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_REG) {
      struct stat file_stat;
      char file_path[512];
      snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
      if (stat(file_path, &file_stat) == 0) {
        char modified_time[20], accessed_time[20], created_time[20];
        strftime(modified_time, sizeof(modified_time), "%Y-%m-%d %H:%M:%S",
                 localtime(&file_stat.st_mtime));
        strftime(accessed_time, sizeof(accessed_time), "%Y-%m-%d %H:%M:%S",
                 localtime(&file_stat.st_atime));
        strftime(created_time, sizeof(created_time), "%Y-%m-%d %H:%M:%S",
                 localtime(&file_stat.st_ctime));

        snprintf(buffer + current_length, buffer_size - current_length,
                 "%-25s%-25s%-25s%-25s\n", entry->d_name, modified_time,
                 accessed_time, created_time);
        current_length = strlen(buffer);
      }
    }
  }
  closedir(dir);

  printf("Generated file list:\n%s\n", buffer);
}
