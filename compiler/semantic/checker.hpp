#pragma once
#include <vector>

#include "../ast/ast.hpp"
#include "../diagnostics.hpp"

namespace ipx {

// Resolves names and checks types. Annotates the AST (Expr::type, Stmt::declares).
// Returns every error found; an empty vector means the program is valid.
std::vector<Diagnostic> checkProgram(Program& program);

}  // namespace ipx
