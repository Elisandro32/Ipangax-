#pragma once
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace ipx {

// endCol is exclusive and refers to the same line as 'line'.
struct Diagnostic {
  std::string message;
  std::string hint;
  int line = 0, col = 0, endCol = 0;
  bool runtime = false;
};

struct DiagnosticError : std::exception {
  Diagnostic d;
  explicit DiagnosticError(Diagnostic x) : d(std::move(x)) {}
  const char* what() const noexcept override { return d.message.c_str(); }
};

std::string formatDiagnostic(const Diagnostic& d, const std::string& file,
                             const std::string& source);

}  // namespace ipx
