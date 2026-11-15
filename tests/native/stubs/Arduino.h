#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define ARDUINO 10800
#define OUTPUT 1
#define HIGH 1
#define LOW 0
using byte = uint8_t;
template<class T> inline T max(T a, T b) { return a > b ? a : b; }
namespace NativeArduino {
inline uint32_t &now() { static uint32_t n = 0; return n; }
inline int *digital() { static int pins[64] = {}; return pins; }
inline int *analog() { static int pins[64] = {}; return pins; }
inline unsigned &delays() { static unsigned n = 0; return n; }
}
inline uint32_t millis() { return NativeArduino::now(); }
inline void delay(uint32_t ms) { ++NativeArduino::delays(); NativeArduino::now() += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { NativeArduino::digital()[pin] = value; }
inline void analogWrite(int pin, int value) { NativeArduino::analog()[pin] = value; }
