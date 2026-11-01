#pragma once
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "diagnostics.hpp"

namespace ipx {

constexpr const char* VERSION = "0.5.0-dev";

struct RunOptions {
  unsigned long long maxSteps = 0;  // 0 = unlimited
  int maxDepth = 1000;              // maximum call depth
  unsigned long long maxArray = 10000000;  // maximum array length
  std::istream* in = nullptr;       // source for input(); null means input() is unavailable
};

// Lex + parse + type-check. Empty result means the program is valid.
std::vector<Diagnostic> check(const std::string& source);

// Check, then interpret. Program output goes to 'out'. Returns false and fills
// 'diags' on any compile-time or runtime error.
bool run(const std::string& source, std::ostream& out, std::vector<Diagnostic>& diags,
         RunOptions opts = {});

}  // namespace ipx
