#pragma once

#include <stddef.h>
#include <string.h>

namespace island_link {
// App CLI transports can append CR/LF or other ASCII whitespace. Only ilink
// commands call this helper; password and Wi-Fi values retain their bytes.
inline void trimCommandWhitespace(char* command) {
  size_t length = strlen(command);
  while (length) {
    const char c = command[length - 1];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\v' && c != '\f') break;
    command[--length] = '\0';
  }
}
}
