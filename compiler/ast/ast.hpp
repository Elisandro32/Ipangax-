#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../lexer/lexer.hpp"
#include "../types/types.hpp"

namespace ipx {

struct Span {
  int line = 0, col = 0, endCol = 0;
};

struct Stmt;

// Built-in function ids, resolved by the checker.
enum class Bi {
  None, Print, Input, Len, Split, Join, Push, Pop, Copy, Make, Sort, Min, Max, Abs, Pow, Sqrt, Range,
  ConvInt, ConvFloat, ConvString,
  Gcd, Lcm, Floor, Ceil, Round, Log, Log2, Log10, Exp, Sin, Cos, Tan, Atan2, Powmod,
  Has, Remove, Get, Keys, Values, First, Last, Add, Top, PushFront, PopFront, Front, Back,
  Construct
};
Bi builtinId(const std::string& name);

enum class ExprKind { Int, Float, String, Bool, Char, Var, Unary, Binary, Call, Array, Index, Ternary, Field, MapLit, SetLit };

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr {
  ExprKind kind = ExprKind::Int;
  Span span;
  int64_t i = 0;      // Int, Bool (0/1), Char code
  double f = 0;       // Float
  std::string s;      // String value, Var/Call name, operator text
  Tok op = Tok::End;  // Unary/Binary operator
  std::vector<ExprP> args;  // Unary: 1, Binary: 2, Call: n
  Type type = Type::Error;  // filled by the checker
  int slot = -1;            // Var: variable slot in its frame
  bool global = false;      // Var: a top-level const read from inside a function
  Stmt* callee = nullptr;   // Call: user function
  Bi bi = Bi::None;         // Call: built-in
  std::vector<std::string> argNames;  // Call: argument names ("" when positional)
  std::vector<int> fieldIdx;          // Construct: field index of each argument
};

struct Param {
  Type type = Type::Error;
  std::string name;
  Span span;
  int slot = 0;
};

enum class StmtKind { VarDecl, Assign, IndexAssign, ExprStmt, If, While, For, Break, Continue, Return, Fn, Match, DoWhile, StructDecl };

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct Arm {  // one arm of a match: values to compare against, and the block to run
  std::vector<ExprP> values;
  std::vector<StmtP> body;
};

struct Stmt {
  StmtKind kind = StmtKind::ExprStmt;
  Span span, nameSpan;
  std::string name;
  bool isConst = false, hasType = false, hasElse = false;
  int slot = -1;      // VarDecl, Assign, For: slot of the variable
  int numSlots = 0;   // Fn: number of variable slots a call needs
  bool declares = false;  // Assign: set by the checker when it introduces a new variable
  Type declType = Type::Error;
  Type retType = Type::Void;
  ExprP target;  // IndexAssign: the a[i] expression being assigned
  ExprP expr;  // initializer, value, condition, return value or expression statement
  std::vector<StmtP> body, elseBody;
  std::vector<Arm> arms;  // Match
  std::vector<Param> params;
};

struct Program {
  std::vector<StmtP> stmts;
  int numGlobals = 0;  // slots needed by top-level code
};

ExprP cloneExpr(const Expr& e);

// S-expression rendering of the AST, one top-level statement per line (used by tests).
std::string dump(const Program& p);

}  // namespace ipx
