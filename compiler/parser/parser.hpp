#pragma once
#include <vector>

#include "../ast/ast.hpp"
#include "../lexer/lexer.hpp"

namespace ipx {

// Throws DiagnosticError on the first syntax error.
Program parse(const std::vector<Token>& tokens);

}  // namespace ipx
