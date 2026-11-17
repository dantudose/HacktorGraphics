#pragma once

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>

struct TestSerial {
  std::string output;

  void printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(length >= 0 && static_cast<unsigned>(length) < sizeof(buffer));
    output.append(buffer, static_cast<unsigned>(length));
  }

  void println() { output += '\n'; }
};

extern TestSerial Serial;
