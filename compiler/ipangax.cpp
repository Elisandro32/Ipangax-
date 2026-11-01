#include "ipangax.hpp"

#include <ostream>

#include "interpreter/interpreter.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "semantic/checker.hpp"

namespace ipx {
namespace {

bool frontEnd(const std::string& src, Program& prog, std::vector<Diagnostic>& diags) {
  try {
    prog = parse(lex(src));
  } catch (const DiagnosticError& e) {
    diags.push_back(e.d);
    return false;
  }
  diags = checkProgram(prog);
  return diags.empty();
}

}  // namespace

std::vector<Diagnostic> check(const std::string& source) {
  Program prog;
  std::vector<Diagnostic> diags;
  frontEnd(source, prog, diags);
  return diags;
}

bool run(const std::string& source, std::ostream& out, std::vector<Diagnostic>& diags,
         RunOptions opts) {
  Program prog;
  if (!frontEnd(source, prog, diags)) return false;
  try {
    interpret(prog, out, opts);
  } catch (const DiagnosticError& e) {
    diags.push_back(e.d);
    return false;
  }
  return true;
}

}  // namespace ipx
