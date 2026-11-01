#pragma once
#include <iosfwd>

#include "../ast/ast.hpp"
#include "../ipangax.hpp"

namespace ipx {

// Executes a program that already passed checkProgram().
// Throws DiagnosticError (runtime = true) on runtime errors.
void interpret(Program& program, std::ostream& out, const RunOptions& opts);

}  // namespace ipx
