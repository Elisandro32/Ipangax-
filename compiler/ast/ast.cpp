#include "ast.hpp"

#include <sstream>

namespace ipx {
namespace {

std::string dumpExpr(const Expr& e) {
  switch (e.kind) {
    case ExprKind::Int: return std::to_string(e.i);
    case ExprKind::Float: { std::ostringstream o; o << e.f; return o.str(); }
    case ExprKind::String: return "\"" + e.s + "\"";
    case ExprKind::Bool: return e.i ? "true" : "false";
    case ExprKind::Char: return std::string("'") + (char)e.i + "'";
    case ExprKind::Var: return e.s;
    case ExprKind::Unary: return "(" + e.s + " " + dumpExpr(*e.args[0]) + ")";
    case ExprKind::Binary:
      return "(" + e.s + " " + dumpExpr(*e.args[0]) + " " + dumpExpr(*e.args[1]) + ")";
    case ExprKind::Array: {
      std::string r = "(array";
      for (auto& a : e.args) r += " " + dumpExpr(*a);
      return r + ")";
    }
    case ExprKind::Index: return "(index " + dumpExpr(*e.args[0]) + " " + dumpExpr(*e.args[1]) + ")";
    case ExprKind::Ternary:
      return "(? " + dumpExpr(*e.args[0]) + " " + dumpExpr(*e.args[1]) + " " + dumpExpr(*e.args[2]) + ")";
    case ExprKind::Field: return "(field " + dumpExpr(*e.args[0]) + " " + e.s + ")";
    case ExprKind::MapLit: {
      std::string r = "(map";
      for (auto& a : e.args) r += " " + dumpExpr(*a);
      return r + ")";
    }
    case ExprKind::SetLit: {
      std::string r = "(set";
      for (auto& a : e.args) r += " " + dumpExpr(*a);
      return r + ")";
    }
    case ExprKind::Call: {
      std::string r = "(call " + e.s;
      for (size_t k = 0; k < e.args.size(); ++k)
        r += " " + (k < e.argNames.size() && !e.argNames[k].empty() ? e.argNames[k] + "=" : "") +
             dumpExpr(*e.args[k]);
      return r + ")";
    }
  }
  return "?";
}

std::string dumpStmt(const Stmt& s);

std::string dumpList(const std::vector<StmtP>& v) {
  std::string r;
  for (auto& s : v) r += " " + dumpStmt(*s);
  return r;
}

std::string dumpStmt(const Stmt& s) {
  switch (s.kind) {
    case StmtKind::VarDecl: {
      std::string r = s.isConst ? "(const " : "(decl ";
      if (s.hasType) r += typeName(s.declType) + " ";
      return r + s.name + " " + dumpExpr(*s.expr) + ")";
    }
    case StmtKind::Assign: return "(assign " + s.name + " " + dumpExpr(*s.expr) + ")";
    case StmtKind::IndexAssign: return "(set " + dumpExpr(*s.target) + " " + dumpExpr(*s.expr) + ")";
    case StmtKind::For: return "(for " + s.name + " " + dumpExpr(*s.expr) + dumpList(s.body) + ")";
    case StmtKind::Match: {
      std::string r = "(match " + dumpExpr(*s.expr);
      for (auto& arm : s.arms) {
        r += " (arm (";
        for (size_t k = 0; k < arm.values.size(); ++k) r += (k ? " " : "") + dumpExpr(*arm.values[k]);
        r += ")" + dumpList(arm.body) + ")";
      }
      if (s.hasElse) r += " (else" + dumpList(s.elseBody) + ")";
      return r + ")";
    }
    case StmtKind::DoWhile: return "(do" + dumpList(s.body) + " while " + dumpExpr(*s.expr) + ")";
    case StmtKind::StructDecl: {
      std::string r = "(struct " + s.name;
      for (auto& f : s.params) r += " (" + typeName(f.type) + " " + f.name + ")";
      return r + ")";
    }
    case StmtKind::Break: return "(break)";
    case StmtKind::Continue: return "(continue)";
    case StmtKind::ExprStmt: return "(expr " + dumpExpr(*s.expr) + ")";
    case StmtKind::If: {
      std::string r = "(if " + dumpExpr(*s.expr) + " (then" + dumpList(s.body) + ")";
      if (s.hasElse) r += " (else" + dumpList(s.elseBody) + ")";
      return r + ")";
    }
    case StmtKind::While: return "(while " + dumpExpr(*s.expr) + dumpList(s.body) + ")";
    case StmtKind::Return:
      return s.expr ? "(return " + dumpExpr(*s.expr) + ")" : "(return)";
    case StmtKind::Fn: {
      std::string r = "(fn " + s.name + " (";
      for (size_t k = 0; k < s.params.size(); ++k) {
        if (k) r += " ";
        r += std::string("(") + typeName(s.params[k].type) + " " + s.params[k].name + ")";
      }
      return r + ") " + typeName(s.retType) + dumpList(s.body) + ")";
    }
  }
  return "?";
}

}  // namespace

ExprP cloneExpr(const Expr& e) {
  auto c = std::make_unique<Expr>();
  c->kind = e.kind;
  c->span = e.span;
  c->i = e.i;
  c->f = e.f;
  c->s = e.s;
  c->op = e.op;
  c->type = e.type;
  c->argNames = e.argNames;
  for (auto& a : e.args) c->args.push_back(cloneExpr(*a));
  return c;
}

Bi builtinId(const std::string& n) {
  static const std::pair<const char*, Bi> table[] = {
      {"print", Bi::Print}, {"input", Bi::Input}, {"len", Bi::Len},   {"split", Bi::Split},
      {"join", Bi::Join},   {"push", Bi::Push},   {"pop", Bi::Pop},   {"copy", Bi::Copy},
      {"make", Bi::Make},   {"sort", Bi::Sort},   {"min", Bi::Min},   {"max", Bi::Max},
      {"abs", Bi::Abs},     {"pow", Bi::Pow},     {"sqrt", Bi::Sqrt}, {"range", Bi::Range},
      {"gcd", Bi::Gcd},     {"lcm", Bi::Lcm},     {"floor", Bi::Floor}, {"ceil", Bi::Ceil},
      {"round", Bi::Round}, {"log", Bi::Log},     {"log2", Bi::Log2}, {"log10", Bi::Log10},
      {"exp", Bi::Exp},     {"sin", Bi::Sin},     {"cos", Bi::Cos},   {"tan", Bi::Tan},
      {"atan2", Bi::Atan2}, {"powmod", Bi::Powmod}, {"has", Bi::Has}, {"remove", Bi::Remove},
      {"get", Bi::Get},     {"keys", Bi::Keys},   {"values", Bi::Values}, {"first", Bi::First},
      {"last", Bi::Last},   {"add", Bi::Add},     {"top", Bi::Top},   {"push_front", Bi::PushFront},
      {"pop_front", Bi::PopFront}, {"front", Bi::Front}, {"back", Bi::Back}};
  for (auto& t : table)
    if (n == t.first) return t.second;
  return Bi::None;
}

std::string dump(const Program& p) {
  std::string r;
  for (size_t k = 0; k < p.stmts.size(); ++k) {
    if (k) r += "\n";
    r += dumpStmt(*p.stmts[k]);
  }
  return r;
}

}  // namespace ipx
