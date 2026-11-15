#pragma once
#include "../../native/stubs/Arduino.h"
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
};
