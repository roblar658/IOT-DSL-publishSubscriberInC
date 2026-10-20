#ifndef COMMON_H
#define COMMON_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define SOCKET_ERROR_VAL SOCKET_ERROR
#define close_socket closesocket
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define sleep_ms(ms) Sleep(ms)
#else
#define PLATFORM_POSIX 1
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET_VAL (-1)
#define SOCKET_ERROR_VAL (-1)
#define close_socket close
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

#define MAX_TAG_NAME_LEN 64
#define MAX_HOST_LEN 128
#define MAX_PAYLOAD_LEN 2048
#define MAX_TAGS 256
#define MAX_ROUTES 512

static inline char *safe_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s) + 1;
  char *copy = (char *)malloc(len);
  if (copy) {
    memcpy(copy, s, len);
  }
  return copy;
}

static inline void trim_whitespace(char *str) {
  if (!str)
    return;
  char *start = str;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  if (start != str) {
    memmove(str, start, strlen(start) + 1);
  }
  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) {
    str[len - 1] = '\0';
    len--;
  }
}

#endif // COMMON_H
