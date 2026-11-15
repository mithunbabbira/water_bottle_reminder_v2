#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <stdint.h>
class Preferences {
 public:
  struct Value { std::string type; std::vector<uint8_t> bytes; };
  struct State {
    std::map<std::string, Value> keys;
    bool fail = false;
    unsigned writes = 0;
    std::string lastWrite;
  };
  static State &state() { static State s; return s; }
  static void reset() { state() = State(); }
  bool begin(const char *, bool) { return true; }
  bool isKey(const char *key) { return state().keys.count(key) != 0; }
  bool remove(const char *key) {
    if (state().fail) return false;
    return state().keys.erase(key) != 0;
  }
  template<class T> size_t put(const char *key, T value, const char *type) {
    return bytes(key, &value, sizeof(value), type);
  }
  template<class T> T get(const char *key, T fallback, const char *type) {
    auto it = state().keys.find(key);
    if (it == state().keys.end() || it->second.type != type || it->second.bytes.size() != sizeof(T)) return fallback;
    T result; memcpy(&result, it->second.bytes.data(), sizeof(T)); return result;
  }
  size_t bytes(const char *key, const void *p, size_t n, const char *type) {
    ++state().writes; state().lastWrite = key;
    if (state().fail) return 0;
    Value v; v.type = type; const uint8_t *b = static_cast<const uint8_t *>(p); v.bytes.assign(b, b + n);
    state().keys[key] = v; return n;
  }
  size_t putBytes(const char *k, const void *p, size_t n) { return bytes(k, p, n, "blob"); }
  size_t getBytesLength(const char *k) { return isKey(k) && state().keys[k].type == "blob" ? state().keys[k].bytes.size() : 0; }
  size_t getBytes(const char *k, void *p, size_t n) {
    const size_t len = getBytesLength(k);
    if (len == 0 || len > n) return 0;
    memcpy(p, state().keys[k].bytes.data(), len); return len;
  }
  size_t putString(const char *k, const char *s) { return bytes(k, s, strlen(s) + 1, "string") - 1; }
  size_t getString(const char *k, char *p, size_t n) {
    if (!isKey(k) || state().keys[k].type != "string") return 0;
    auto &b = state().keys[k].bytes;
    if (b.size() > n) return 0;
    memcpy(p, b.data(), b.size()); return b.size();
  }
#define NATIVE_PREF(Type, Name, Tag) \
  size_t put##Name(const char *k, Type v) { return put<Type>(k, v, Tag); } \
  Type get##Name(const char *k, Type d = Type()) { return get<Type>(k, d, Tag); }
  NATIVE_PREF(uint8_t, UChar, "u8")
  NATIVE_PREF(uint16_t, UShort, "u16")
  NATIVE_PREF(int16_t, Short, "i16")
  NATIVE_PREF(uint32_t, UInt, "u32")
  NATIVE_PREF(int32_t, Long, "i32")
  NATIVE_PREF(int32_t, Int, "i32")
  NATIVE_PREF(float, Float, "float")
  NATIVE_PREF(bool, Bool, "bool")
#undef NATIVE_PREF
};
