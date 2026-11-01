#include "diagnostics.hpp"

#include <algorithm>
#include <sstream>

namespace ipx {

std::string formatDiagnostic(const Diagnostic& d, const std::string& file,
                             const std::string& src) {
  std::ostringstream o;
  o << (d.runtime ? "Ipangax Runtime Error" : "Ipangax Error") << "\n\n";
  o << file << ":" << d.line << ":" << d.col << "\n\n" << d.message << "\n";

  size_t pos = 0;
  int cur = 1;
  while (cur < d.line && pos < src.size()) {
    size_t nl = src.find('\n', pos);
    if (nl == std::string::npos) { pos = src.size(); break; }
    pos = nl + 1;
    ++cur;
  }
  if (d.line > 0 && cur == d.line) {
    size_t nl = src.find('\n', pos);
    std::string text = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    std::replace(text.begin(), text.end(), '\t', ' ');
    std::string num = std::to_string(d.line);
    o << "\n" << num << " | " << text << "\n";
    o << std::string(num.size() + 3 + std::max(0, d.col - 1), ' ')
      << std::string(std::max(1, d.endCol - d.col), '^') << "\n";
  }
  if (!d.hint.empty()) o << "\nhelp: " << d.hint << "\n";
  return o.str();
}

}  // namespace ipx
