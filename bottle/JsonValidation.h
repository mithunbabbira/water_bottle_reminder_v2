#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "JsonFrame.h"

// C-string consumers must not see a different command/credential than the JSON
// parser. ArduinoJson preserves embedded NUL lengths; reject those explicitly.
inline bool validJsonStrings(JsonVariantConst v) {
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst p : v.as<JsonObjectConst>()) {
      if (p.key().size() != strlen(p.key().c_str()) || !validJsonStrings(p.value())) return false;
    }
  } else if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst item : v.as<JsonArrayConst>()) if (!validJsonStrings(item)) return false;
  } else if (v.is<JsonString>()) {
    JsonString s = v.as<JsonString>();
    if (s.size() != strlen(s.c_str())) return false;
  }
  return true;
}
