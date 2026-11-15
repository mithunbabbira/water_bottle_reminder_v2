#include "../bottle/JsonValidation.h"
#include <cassert>
#include <iostream>

bool valid(const char *s) {
  JsonDocument d;
  return !deserializeJson(d,s) && singleJsonObject(s) && validJsonStrings(d.as<JsonVariantConst>());
}
int main() {
  assert(valid("{}"));
  assert(valid(" \t{\"s\":\"}\\\"{\",\"a\":[1,{}]}\r\n"));
  assert(valid(R"({"id":1,"cmd":"wifi.add","ssid":"Office guest network","password":"a password with spaces"})"));
  assert(valid(R"({"id":1,"cmd":"status","x":"\\u0000"})"));
  assert(!valid("{}junk"));
  assert(!valid("{}{}"));
  assert(!valid("[{}]"));
  assert(!valid("{\"s\":\"x"));
  assert(!valid("{"));
  assert(!valid(""));
  assert(!valid(R"({"id":1,"cmd":"tare\u0000ignored"})"));
  assert(!valid(R"({"id":1,"cmd":"wifi.add","ssid":"a\u0000b"})"));
  assert(!valid(R"({"id":1,"cmd":"wifi.add","password":"a\u0000b"})"));
  assert(!valid(R"({"id":1,"cmd\u0000ignored":"tare"})"));
  std::cout << "14 JSON boundary/string regression cases passed\n";
}
