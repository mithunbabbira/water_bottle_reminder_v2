#pragma once
#include <stddef.h>

// ArduinoJson intentionally stops at the first complete document. Commands must
// instead contain exactly one object, not an object followed by ignored bytes.
inline bool singleJsonObject(const char *s) {
  if (!s) return false;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') ++i;
  if (s[i++] != '{') return false;
  int depth = 1;
  bool quoted = false, escaped = false;
  for (; s[i]; ++i) {
    const char c = s[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') quoted = true;
    else if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') {
      if (--depth == 0) {
        if (c != '}') return false;
        ++i;
        while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') ++i;
        return s[i] == 0;
      }
    }
  }
  return false;
}
