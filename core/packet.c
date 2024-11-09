#include "./packet.h"
#include <libgen.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

ssize_t safe_recv(int socket, void *buffer, size_t amount, int flags) {
  ssize_t read = 0;
  while (read < amount) {
    if ((read += recv(socket, buffer + read, amount - read, flags)) == 0)
      return 0;
  }
  return amount;
}
