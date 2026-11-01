#include "checker.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace ipx {
namespace {

struct VarInfo {
  Type type;
  bool isConst;
  int slot = 0;
};

struct FnSig {
  std::vector<Type> params;
  Type ret;
  Stmt* decl = nullptr;
};

// Core built-ins: a user function may not reuse these names.
const std::set<std::string>& builtinNames() {
  static const std::set<std::string> n = {"print", "input", "len",  "split", "join", "push",
                                          "pop",   "copy",  "make", "sort",  "min",  "max",
                                          "abs",   "pow",   "sqrt", "range"};
  return n;
}

// Newer built-ins: a user function with the same name takes precedence.
const std::set<std::string>& softBuiltinNames() {
  static const std::set<std::string> n = {
      "gcd",    "lcm",    "floor",      "ceil",      "round", "log",   "log2",
      "log10",  "exp",    "sin",        "cos",       "tan",   "atan2", "powmod",
      "has",    "remove", "get",        "keys",      "values", "first", "last",
      "add",    "top",    "push_front", "pop_front", "front", "back"};
  return n;
}

int editDistance(const std::string& a, const std::string& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = (int)i;
    for (size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

std::string convHint(Type from, Type to) {
  if (from == Type::Error || from == Type::Void) return "";
  bool ok = (from == Type::Int && to == Type::Float) || (from == Type::Float && to == Type::Int) ||
            (from == Type::String && (to == Type::Int || to == Type::Float)) ||
            (from == Type::Char && to == Type::Int) || to == Type::String;
  return ok ? "Use " + typeName(to) + "(value) to convert explicitly." : "";
}

bool isKeyType(Type t) {
  return t == Type::Int || t == Type::String || t == Type::Char || t == Type::Bool;
}
bool isOrdered(Type t) {
  return t == Type::Int || t == Type::Float || t == Type::String || t == Type::Char;
}

class Checker {
 public:
  std::vector<Diagnostic> run(Program& p) {
    collect(p);
    scopes.emplace_back();
    for (auto& s : p.stmts) checkStmt(*s);
    p.numGlobals = nextSlot;
    return std::move(diags);
  }

 private:
  std::vector<Diagnostic> diags;
  std::map<std::string, FnSig> fns;
  std::map<std::string, Stmt*> structs;
  std::vector<std::map<std::string, VarInfo>> scopes;
  bool inFn = false;
  int loopDepth = 0;
  int nextSlot = 0;
  Type curRet = Type::Void;
  std::string curFn;

  void error(const Span& sp, const std::string& msg, const std::string& hint = "") {
    Diagnostic d;
    d.message = msg;
    d.hint = hint;
    d.line = sp.line;
    d.col = sp.col;
    d.endCol = sp.endCol;
    diags.push_back(d);
  }

  static std::string q(const std::string& s) { return "'" + s + "'"; }

  // ---- types

  void checkKey(Type k, const Span& sp) {
    if (k != Type::Error && !isKeyType(k))
      error(sp, "Map keys and set elements must be int, string, char or bool, got " +
                    q(typeName(k)) + ".");
  }

  void validateType(Type t, const Span& sp) {
    if (!isCompound(t)) return;
    const TypeInfo& ti = info(t);
    switch (ti.kind) {
      case Kind::Array:
      case Kind::Deque: validateType(ti.a, sp); break;
      case Kind::Set: checkKey(ti.a, sp); break;
      case Kind::Map:
        checkKey(ti.a, sp);
        validateType(ti.b, sp);
        break;
      case Kind::Heap:
      case Kind::MaxHeap:
        if (!isOrdered(ti.a))
          error(sp, "Heap elements must be int, float, string or char, got " + q(typeName(ti.a)) + ".");
        break;
      case Kind::Struct: break;
    }
  }

  // Can a value of type 'from' (the expression e) be used where 'to' is expected?
  // Empty [] and {} literals take the type they are used as.
  bool convertTo(Expr& e, Type from, Type to) {
    if (from == to) return true;
    if (from == Type::EmptyArray && isArray(to)) {
      e.type = to;
      return true;
    }
    if (from == Type::EmptyBrace && (isMap(to) || isSet(to) || isHeap(to) || isDeque(to))) {
      e.type = to;
      return true;
    }
    return false;
  }

  void need(Expr& arg, Type got, Type want, const std::string& what) {
    if (got != Type::Error && !convertTo(arg, got, want))
      error(arg.span, what + " must be " + q(typeName(want)) + ", got " + q(typeName(got)) + ".",
            convHint(got, want));
  }

  // ---- declarations

  void collect(Program& p) {
    for (auto& s : p.stmts) {
      if (s->kind != StmtKind::StructDecl) continue;
      if (structs.count(s->name)) {
        error(s->nameSpan, "Struct " + q(s->name) + " is already defined.");
        continue;
      }
      structs[s->name] = s.get();
      TypeInfo& ti = infoMut(s->declType);
      ti.fields.clear();
      std::set<std::string> seen;
      for (auto& f : s->params) {
        if (!seen.insert(f.name).second) {
          error(f.span, "Field " + q(f.name) + " is declared twice.");
          continue;
        }
        validateType(f.type, f.span);
        if (f.type == s->declType)
          error(f.span, "A struct cannot contain itself directly.",
                "Use an array instead, for example: " + s->name + "[] children");
        ti.fields.push_back({f.name, f.type});
      }
    }
    for (auto& s : p.stmts) {
      if (s->kind != StmtKind::Fn) continue;
      if (builtinNames().count(s->name)) {
        error(s->nameSpan, "'" + s->name + "' is a built-in function and cannot be redefined.");
        continue;
      }
      if (structs.count(s->name)) {
        error(s->nameSpan, q(s->name) + " is already defined as a struct.");
        continue;
      }
      if (fns.count(s->name)) {
        error(s->nameSpan, "Function " + q(s->name) + " is already defined.");
        continue;
      }
      FnSig sig;
      sig.ret = s->retType;
      for (auto& pr : s->params) sig.params.push_back(pr.type);
      sig.decl = s.get();
      fns[s->name] = sig;
      if (s->name == "main") {
        if (!s->params.empty())
          error(s->nameSpan, "Function 'main' cannot take parameters in v0.1.");
        if (s->retType != Type::Void)
          error(s->nameSpan, "Function 'main' cannot return a value in v0.1.",
                "Remove the '-> type' part of the declaration.");
      }
    }
  }

  VarInfo* lookup(const std::string& n, bool* hidden = nullptr, bool* atGlobal = nullptr) {
    for (int k = (int)scopes.size() - 1; k >= 0; --k) {
      auto it = scopes[k].find(n);
      if (it == scopes[k].end()) continue;
      if (k == 0 && inFn && !it->second.isConst) {
        if (hidden) *hidden = true;
        return nullptr;
      }
      if (atGlobal) *atGlobal = (k == 0 && inFn);
      return &it->second;
    }
    return nullptr;
  }

  std::string suggest(const std::string& name, const std::set<std::string>& cands) {
    std::string best;
    int bestD = 3;
    for (auto& c : cands) {
      int d = editDistance(name, c);
      if (d < bestD && d <= (int)std::max<size_t>(1, name.size() / 3)) {
        bestD = d;
        best = c;
      }
    }
    return best.empty() ? "" : "Did you mean " + q(best) + "?";
  }

  void block(std::vector<StmtP>& body) {
    scopes.emplace_back();
    for (auto& s : body) checkStmt(*s);
    scopes.pop_back();
  }

  static bool returns(const std::vector<StmtP>& body) {
    for (auto& s : body) {
      if (s->kind == StmtKind::Return) return true;
      if (s->kind == StmtKind::If && s->hasElse && returns(s->body) && returns(s->elseBody))
        return true;
      if (s->kind == StmtKind::Match && s->hasElse && returns(s->elseBody)) {
        bool all = true;
        for (auto& arm : s->arms) all = all && returns(arm.body);
        if (all) return true;
      }
    }
    return false;
  }

  void condition(Expr& e, const char* kw) {
    Type t = valueExpr(e);
    if (t != Type::Error && t != Type::Bool)
      error(e.span, std::string("The condition of '") + kw + "' must be 'bool', got " +
                        q(typeName(t)) + ".",
            t == Type::Int ? "Use a comparison, for example x != 0." : "");
  }

  // ---- statements

  void checkStmt(Stmt& s) {
    switch (s.kind) {
      case StmtKind::VarDecl: checkDecl(s); break;
      case StmtKind::Assign: checkAssign(s); break;
      case StmtKind::IndexAssign: checkIndexAssign(s); break;
      case StmtKind::ExprStmt:
        checkExpr(*s.expr);
        if (s.expr->kind != ExprKind::Call)
          error(s.expr->span, "This expression has no effect.",
                "Only function calls can be used as statements.");
        break;
      case StmtKind::If:
        condition(*s.expr, "if");
        block(s.body);
        if (s.hasElse) block(s.elseBody);
        break;
      case StmtKind::While:
        condition(*s.expr, "while");
        ++loopDepth;
        block(s.body);
        --loopDepth;
        break;
      case StmtKind::For: checkFor(s); break;
      case StmtKind::Match: checkMatch(s); break;
      case StmtKind::DoWhile:
        ++loopDepth;
        block(s.body);
        --loopDepth;
        condition(*s.expr, "do-while");
        break;
      case StmtKind::Break:
      case StmtKind::Continue:
        if (loopDepth == 0)
          error(s.span, std::string("'") + (s.kind == StmtKind::Break ? "break" : "continue") +
                            "' can only be used inside a loop.");
        break;
      case StmtKind::Return: checkReturn(s); break;
      case StmtKind::Fn: checkFn(s); break;
      case StmtKind::StructDecl: break;  // handled in collect()
    }
  }

  void checkMatch(Stmt& s) {
    Type st = valueExpr(*s.expr);
    if (st != Type::Error && st != Type::Int && st != Type::String && st != Type::Char &&
        st != Type::Bool)
      error(s.expr->span, "'match' needs an int, string, char or bool, got " + q(typeName(st)) + ".",
            st == Type::Float ? "Floats cannot be compared exactly; use if / else if." : "");
    if (s.arms.empty()) error(s.span, "A 'match' needs at least one arm besides 'else'.");
    for (auto& arm : s.arms) {
      for (auto& v : arm.values) {
        Type vt = valueExpr(*v);
        if (vt != Type::Error && st != Type::Error && vt != st)
          error(v->span, "This arm has type " + q(typeName(vt)) + " but the match value has type " +
                             q(typeName(st)) + ".",
                convHint(vt, st));
      }
      block(arm.body);
    }
    if (s.hasElse) block(s.elseBody);
  }

  void checkFor(Stmt& s) {
    Expr& it = *s.expr;
    Type elem = Type::Error;
    if (it.kind == ExprKind::Call && it.s == "range" && !fns.count("range")) {
      it.bi = Bi::Range;
      size_t n = it.args.size();
      if (n < 1 || n > 3)
        error(it.span, "range() takes 1 to 3 arguments but got " + std::to_string(n) + ".");
      for (size_t k = 0; k < n; ++k)
        need(*it.args[k], valueExpr(*it.args[k]), Type::Int,
             "Argument " + std::to_string(k + 1) + " of 'range'");
      elem = Type::Int;
    } else {
      Type t = valueExpr(it);
      if (isArray(t) || isSet(t) || isDeque(t)) elem = elemOf(t);
      else if (isMap(t)) elem = keyOf(t);
      else if (t == Type::String) elem = Type::Char;
      else if (t != Type::Error && t != Type::EmptyArray && t != Type::EmptyBrace)
        error(it.span, "Cannot loop over a value of type " + q(typeName(t)) + ".",
              "Use range(n) to repeat n times, or loop over an array, string, map, set or deque.");
    }
    scopes.emplace_back();
    s.slot = nextSlot++;
    scopes.back()[s.name] = {elem, false, s.slot};
    ++loopDepth;
    for (auto& st : s.body) checkStmt(*st);
    --loopDepth;
    scopes.pop_back();
  }

  int fieldIndex(Expr& fe, Type base) {  // fe is a Field expression; reports errors
    if (!isStruct(base)) {
      if (base != Type::Error)
        error(fe.span, "Cannot access field " + q(fe.s) + " on a value of type " + q(typeName(base)) + ".");
      return -1;
    }
    const auto& fields = info(base).fields;
    std::set<std::string> names;
    for (size_t k = 0; k < fields.size(); ++k) {
      names.insert(fields[k].first);
      if (fields[k].first == fe.s) {
        fe.slot = (int)k;
        return (int)k;
      }
    }
    error(fe.span, "Struct " + q(typeName(base)) + " has no field " + q(fe.s) + ".", suggest(fe.s, names));
    return -1;
  }

  void checkIndexAssign(Stmt& s) {
    Expr& tgt = *s.target;
    if (tgt.kind == ExprKind::Field) {
      Type b = valueExpr(*tgt.args[0]);
      Type v = valueExpr(*s.expr);
      tgt.type = Type::Error;
      int fi = fieldIndex(tgt, b);
      if (fi >= 0) {
        Type ft = info(b).fields[(size_t)fi].second;
        tgt.type = ft;
        if (v != Type::Error && !convertTo(*s.expr, v, ft))
          error(s.expr->span, "Cannot assign a value of type " + q(typeName(v)) + " to field " +
                                  q(tgt.s) + " of type " + q(typeName(ft)) + ".",
                convHint(v, ft));
      }
      return;
    }
    Type b = valueExpr(*tgt.args[0]);
    Type i = valueExpr(*tgt.args[1]);
    Type v = valueExpr(*s.expr);
    tgt.type = Type::Error;
    if (isMap(b)) {
      tgt.slot = 1;
      tgt.type = valueOf(b);
      need(*tgt.args[1], i, keyOf(b), "The key");
      if (v != Type::Error && !convertTo(*s.expr, v, valueOf(b)))
        error(s.expr->span, "Cannot assign a value of type " + q(typeName(v)) +
                                " to a map value of type " + q(typeName(valueOf(b))) + ".",
              convHint(v, valueOf(b)));
      return;
    }
    if (i != Type::Error && i != Type::Int)
      error(tgt.args[1]->span, "An index must be 'int', got " + q(typeName(i)) + ".");
    if (b == Type::String) {
      error(tgt.span, "Strings are immutable; their characters cannot be assigned.");
    } else if (isArray(b) || isDeque(b)) {
      tgt.slot = isDeque(b) ? 2 : 0;
      tgt.type = elemOf(b);
      if (v != Type::Error && !convertTo(*s.expr, v, elemOf(b)))
        error(s.expr->span, "Cannot assign a value of type " + q(typeName(v)) +
                                " to an element of type " + q(typeName(elemOf(b))) + ".",
              convHint(v, elemOf(b)));
    } else if (b != Type::Error) {
      error(tgt.span, "Cannot index a value of type " + q(typeName(b)) + ".");
    }
  }

  void checkReturn(Stmt& s) {
    if (!inFn) {
      error(s.span, "'return' can only be used inside a function.");
      if (s.expr) valueExpr(*s.expr);
      return;
    }
    if (s.expr) {
      Type t = valueExpr(*s.expr);
      if (curRet == Type::Void)
        error(s.expr->span,
              "Function " + q(curFn) + " does not return a value, so 'return' cannot have one.",
              "Add '-> type' to the function declaration.");
      else if (t != Type::Error && !convertTo(*s.expr, t, curRet))
        error(s.expr->span,
              "Cannot return a value of type " + q(typeName(t)) + " from function " + q(curFn) +
                  " declared to return " + q(typeName(curRet)) + ".",
              convHint(t, curRet));
    } else if (curRet != Type::Void) {
      error(s.span, "Function " + q(curFn) + " must return a value of type " +
                        q(typeName(curRet)) + ".");
    }
  }

  void checkFn(Stmt& s) {
    scopes.emplace_back();
    inFn = true;
    curRet = s.retType;
    curFn = s.name;
    int savedLoop = loopDepth;
    loopDepth = 0;
    int savedSlot = nextSlot;
    nextSlot = 0;
    validateType(s.retType, s.nameSpan);
    for (auto& p : s.params) {
      validateType(p.type, p.span);
      if (scopes.back().count(p.name)) {
        error(p.span, "Parameter " + q(p.name) + " is declared twice.");
      } else {
        p.slot = nextSlot++;
        scopes.back()[p.name] = {p.type, false, p.slot};
      }
    }
    for (auto& st : s.body) checkStmt(*st);
    loopDepth = savedLoop;
    s.numSlots = nextSlot;
    nextSlot = savedSlot;
    scopes.pop_back();
    inFn = false;
    if (s.retType != Type::Void && !returns(s.body))
      error(s.nameSpan, "Function " + q(s.name) +
                            " may reach its end without returning a value of type " +
                            q(typeName(s.retType)) + ".");
  }

  bool isConstExpr(const Expr& e) {
    switch (e.kind) {
      case ExprKind::Var: {
        VarInfo* v = lookup(e.s);
        return v && v->isConst;
      }
      case ExprKind::Call: return false;
      case ExprKind::Unary:
      case ExprKind::Binary:
      case ExprKind::Index:
      case ExprKind::Array:
      case ExprKind::Ternary:
        for (auto& a : e.args)
          if (!isConstExpr(*a)) return false;
        return true;
      default: return true;
    }
  }

  void inferError(Expr& e, const std::string& name, Type& t) {
    if (t == Type::EmptyArray) {
      error(e.span, "Cannot infer the type of an empty array.",
            "Declare it with a type, for example: int[] " + name + " = []");
      t = Type::Error;
    } else if (t == Type::EmptyBrace) {
      error(e.span, "Cannot infer the type of '{}'.",
            "Declare it with a type, for example: map[string, int] " + name + " = {}");
      t = Type::Error;
    }
  }

  void checkDecl(Stmt& s) {
    Type t = valueExpr(*s.expr);
    if (s.hasType) validateType(s.declType, s.nameSpan);
    if (s.isConst) {
      Type ct = s.hasType ? s.declType : t;
      if (isArray(ct) || ct == Type::EmptyArray)
        error(s.nameSpan, "Constants cannot be arrays in v0.2.", "Use a normal variable instead.");
      else if (isCompound(ct) || ct == Type::EmptyBrace)
        error(s.nameSpan, "Constants cannot be collections or structs yet.",
              "Use a normal variable instead.");
      else if (t != Type::Error && !isConstExpr(*s.expr))
        error(s.expr->span,
              "Constant " + q(s.name) + " must be initialized with a constant expression.",
              "Use literals, other constants and operators, but not function calls or variables.");
    }
    if (!s.hasType) inferError(*s.expr, s.name, t);
    Type vt = s.hasType ? s.declType : t;
    if (s.hasType && t != Type::Error && !convertTo(*s.expr, t, s.declType))
      error(s.expr->span,
            "Cannot assign a value of type " + q(typeName(t)) + " to variable " + q(s.name) +
                " of type " + q(typeName(s.declType)) + ".",
            convHint(t, s.declType));
    auto& sc = scopes.back();
    if (sc.count(s.name))
      error(s.nameSpan, "Variable " + q(s.name) + " is already declared in this scope.",
            "Assign to it with: " + s.name + " = value");
    else {
      s.slot = nextSlot++;
      sc[s.name] = {vt, s.isConst, s.slot};
    }
  }

  void checkAssign(Stmt& s) {
    Type t = valueExpr(*s.expr);
    bool hidden = false;
    VarInfo* v = lookup(s.name, &hidden);
    s.declares = false;
    if (v) {
      s.slot = v->slot;
      if (v->isConst)
        error(s.nameSpan, "Cannot assign to constant " + q(s.name) + ".",
              "Constants are immutable; use a normal variable if it must change.");
      else if (t != Type::Error && v->type != Type::Error && !convertTo(*s.expr, t, v->type))
        error(s.expr->span,
              "Cannot assign a value of type " + q(typeName(t)) + " to variable " + q(s.name) +
                  " of type " + q(typeName(v->type)) + ".",
              convHint(t, v->type));
    } else if (hidden) {
      error(s.nameSpan, "Variable " + q(s.name) +
                            " is global and cannot be modified inside a function in v0.1.",
            "Pass it as a parameter and return the new value.");
    } else {
      s.declares = true;
      s.slot = nextSlot++;
      inferError(*s.expr, s.name, t);
      scopes.back()[s.name] = {t, false, s.slot};
    }
  }

  // ---- expressions

  Type valueExpr(Expr& e) {
    Type t = checkExpr(e);
    if (t == Type::Void) {
      error(e.span, "This expression has no value.",
            e.kind == ExprKind::Call ? "Function " + q(e.s) + " does not return a value." : "");
      e.type = Type::Error;
      return Type::Error;
    }
    return t;
  }

  Type checkExpr(Expr& e) {
    e.type = compute(e);
    return e.type;
  }

  Type bad(Expr& e, const char* verb, Type a, Type b) {
    std::string hint;
    bool strMix = (a == Type::String) != (b == Type::String);
    auto simple = [](Type t) {
      return t == Type::Int || t == Type::Float || t == Type::Bool || t == Type::Char;
    };
    if (e.op == Tok::Plus && strMix && (simple(a) || simple(b)))
      hint = "Use string(value) to convert the other operand to string.";
    else if ((a == Type::Int && b == Type::Float) || (a == Type::Float && b == Type::Int))
      hint = "Use float(value) or int(value) to convert explicitly.";
    error(e.span, std::string("Cannot ") + verb + " values of type " + q(typeName(a)) + " and " +
                      q(typeName(b)) + ".",
          hint);
    return Type::Error;
  }

  Type compute(Expr& e) {
    switch (e.kind) {
      case ExprKind::Int: return Type::Int;
      case ExprKind::Float: return Type::Float;
      case ExprKind::String: return Type::String;
      case ExprKind::Bool: return Type::Bool;
      case ExprKind::Char: return Type::Char;
      case ExprKind::Var: {
        bool hidden = false, atG = false;
        VarInfo* v = lookup(e.s, &hidden, &atG);
        if (v) {
          e.slot = v->slot;
          e.global = atG;
          return v->type;
        }
        if (hidden) {
          error(e.span, "Variable " + q(e.s) +
                            " is global and cannot be used inside a function in v0.1.",
                "Declare it with 'const' or pass it as a parameter.");
        } else if (e.s == "PI" || e.s == "E") {  // predefined constants, unless the program defines them
          e.kind = ExprKind::Float;
          e.f = e.s == "PI" ? 3.14159265358979323846 : 2.71828182845904523536;
          return Type::Float;
        } else {
          std::set<std::string> names;
          for (auto& sc : scopes)
            for (auto& kv : sc) names.insert(kv.first);
          error(e.span, "Undefined variable " + q(e.s) + ".", suggest(e.s, names));
        }
        return Type::Error;
      }
      case ExprKind::Unary: {
        Type a = valueExpr(*e.args[0]);
        if (a == Type::Error) return a;
        if (e.op == Tok::Minus && (a == Type::Int || a == Type::Float)) return a;
        if (e.op == Tok::Bang && a == Type::Bool) return a;
        if (e.op == Tok::Tilde && a == Type::Int) return a;
        error(e.span, "Operator " + q(e.s) + " cannot be applied to type " + q(typeName(a)) + ".");
        return Type::Error;
      }
      case ExprKind::Binary: return computeBinary(e);
      case ExprKind::Call: return computeCall(e);
      case ExprKind::Array: return computeArray(e);
      case ExprKind::MapLit: return computeMapLit(e);
      case ExprKind::SetLit: return computeSetLit(e);
      case ExprKind::Field: {
        Type b = valueExpr(*e.args[0]);
        int fi = fieldIndex(e, b);
        return fi >= 0 ? info(b).fields[(size_t)fi].second : Type::Error;
      }
      case ExprKind::Ternary: {
        Type c = valueExpr(*e.args[0]);
        Type a = valueExpr(*e.args[1]);
        Type b = valueExpr(*e.args[2]);
        if (c != Type::Error && c != Type::Bool)
          error(e.args[0]->span, "The condition of '?:' must be 'bool', got " + q(typeName(c)) + ".",
                c == Type::Int ? "Use a comparison, for example x != 0." : "");
        if (a == Type::Error || b == Type::Error) return Type::Error;
        if (a == b) return a;
        if (a == Type::EmptyArray && isArray(b)) return b;
        if (b == Type::EmptyArray && isArray(a)) return a;
        error(e.span, "Both branches of '?:' must have the same type, got " + q(typeName(a)) +
                          " and " + q(typeName(b)) + ".",
              convHint(b, a));
        return Type::Error;
      }
      case ExprKind::Index: {
        Type b = valueExpr(*e.args[0]);
        Type i = valueExpr(*e.args[1]);
        e.slot = isMap(b) ? 1 : isDeque(b) ? 2 : 0;  // how the interpreter indexes the base
        if (isMap(b)) {
          need(*e.args[1], i, keyOf(b), "The key");
          return valueOf(b);
        }
        if (i != Type::Error && i != Type::Int)
          error(e.args[1]->span, "An index must be 'int', got " + q(typeName(i)) + ".");
        if (isArray(b) || isDeque(b)) return elemOf(b);
        if (b == Type::String) return Type::Char;
        if (b != Type::Error)
          error(e.span, "Cannot index a value of type " + q(typeName(b)) + ".");
        return Type::Error;
      }
    }
    return Type::Error;
  }

  Type computeArray(Expr& e) {
    if (e.args.empty()) return Type::EmptyArray;
    std::vector<Type> ts;
    Type el = Type::Error;
    bool anyErr = false;
    for (auto& a : e.args) {
      Type t = valueExpr(*a);
      ts.push_back(t);
      if (t == Type::Error) anyErr = true;
      else if (el == Type::Error && t != Type::EmptyArray && t != Type::EmptyBrace) el = t;
    }
    if (el == Type::Error) {
      if (!anyErr) error(e.span, "Cannot infer the type of this array.");
      return Type::Error;
    }
    for (size_t k = 0; k < ts.size(); ++k)
      if (ts[k] != Type::Error && !convertTo(*e.args[k], ts[k], el))
        error(e.args[k]->span, "Array elements must have the same type: expected " +
                                   q(typeName(el)) + ", got " + q(typeName(ts[k])) + ".",
              convHint(ts[k], el));
    return arrayOf(el);
  }

  Type computeSetLit(Expr& e) {
    if (e.args.empty()) return Type::EmptyBrace;
    Type el = Type::Error;
    std::vector<Type> ts;
    bool anyErr = false;
    for (auto& a : e.args) {
      Type t = valueExpr(*a);
      ts.push_back(t);
      if (t == Type::Error) anyErr = true;
      else if (el == Type::Error) el = t;
    }
    if (el == Type::Error) return Type::Error;
    checkKey(el, e.span);
    for (size_t k = 0; k < ts.size(); ++k)
      if (ts[k] != Type::Error && ts[k] != el)
        error(e.args[k]->span, "Set elements must have the same type: expected " + q(typeName(el)) +
                                   ", got " + q(typeName(ts[k])) + ".");
    (void)anyErr;
    return isKeyType(el) ? setOf(el) : Type::Error;
  }

  Type computeMapLit(Expr& e) {
    Type kt = Type::Error, vt = Type::Error;
    std::vector<Type> ts;
    for (auto& a : e.args) {
      Type t = valueExpr(*a);
      ts.push_back(t);
    }
    for (size_t k = 0; k + 1 < ts.size(); k += 2) {
      if (kt == Type::Error && ts[k] != Type::Error) kt = ts[k];
      if (vt == Type::Error && ts[k + 1] != Type::Error && ts[k + 1] != Type::EmptyArray &&
          ts[k + 1] != Type::EmptyBrace)
        vt = ts[k + 1];
    }
    if (kt == Type::Error || vt == Type::Error) return Type::Error;
    checkKey(kt, e.span);
    for (size_t k = 0; k + 1 < ts.size(); k += 2) {
      if (ts[k] != Type::Error && ts[k] != kt)
        error(e.args[k]->span, "Map keys must have the same type: expected " + q(typeName(kt)) +
                                   ", got " + q(typeName(ts[k])) + ".");
      if (ts[k + 1] != Type::Error && !convertTo(*e.args[k + 1], ts[k + 1], vt))
        error(e.args[k + 1]->span, "Map values must have the same type: expected " +
                                       q(typeName(vt)) + ", got " + q(typeName(ts[k + 1])) + ".");
    }
    return isKeyType(kt) ? mapOf(kt, vt) : Type::Error;
  }

  Type computeBinary(Expr& e) {
    Type a = valueExpr(*e.args[0]);
    Type b = valueExpr(*e.args[1]);
    if (a == Type::Error || b == Type::Error) return Type::Error;
    bool num = a == b && (a == Type::Int || a == Type::Float);
    switch (e.op) {
      case Tok::Plus:
        if (num || (a == b && a == Type::String)) return a;
        return bad(e, "add", a, b);
      case Tok::Minus: return num ? a : bad(e, "subtract", a, b);
      case Tok::Star: return num ? a : bad(e, "multiply", a, b);
      case Tok::Slash: return num ? a : bad(e, "divide", a, b);
      case Tok::Percent:
        return (a == Type::Int && b == Type::Int) ? a : bad(e, "take the remainder of", a, b);
      case Tok::Amp: case Tok::Pipe: case Tok::Caret: case Tok::Shl: case Tok::Shr:
        return (a == Type::Int && b == Type::Int) ? a : bad(e, "apply a bitwise operator to", a, b);
      case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
        if (a == b && (a == Type::Int || a == Type::Float || a == Type::String || a == Type::Char))
          return Type::Bool;
        return bad(e, "compare", a, b);
      case Tok::EqEq: case Tok::NotEq:
        return (a == b && !isCompound(a) && a != Type::EmptyArray && a != Type::EmptyBrace)
                   ? Type::Bool
                   : bad(e, "compare", a, b);
      case Tok::AndAnd: case Tok::OrOr:
        if (a == Type::Bool && b == Type::Bool) return Type::Bool;
        error(e.span, "Operator " + q(e.s) + " requires 'bool' operands, got " + q(typeName(a)) +
                          " and " + q(typeName(b)) + ".");
        return Type::Error;
      default: return Type::Error;
    }
  }

  // Checks the argument count; on a mismatch the arguments are still checked and an error is reported.
  bool argc(Expr& e, size_t n) {
    if (e.args.size() == n) return true;
    for (auto& a : e.args) valueExpr(*a);
    error(e.span, q(e.s) + " expects " + std::to_string(n) + (n == 1 ? " argument" : " arguments") +
                      " but got " + std::to_string(e.args.size()) + ".");
    return false;
  }

  static const char* collectionWord(Type t) {
    return isArray(t) ? "an array" : isHeap(t) ? "a heap" : "a deque";
  }

  // float -> float / int built-ins
  void floatFn(Expr& e, Type& r, size_t n, Type result) {
    r = result;
    if (!argc(e, n)) return;
    for (size_t k = 0; k < n; ++k) {
      Type a = valueExpr(*e.args[k]);
      if (a != Type::Error && a != Type::Float)
        error(e.args[k]->span, e.s + "() needs a float, got " + q(typeName(a)) + ".",
              convHint(a, Type::Float));
    }
  }

  bool builtin(Expr& e, Type& r) {
    const std::string& n = e.s;
    if (!builtinNames().count(n) && !softBuiltinNames().count(n)) return false;
    e.bi = builtinId(n);
    auto arg = [&](size_t k) { return valueExpr(*e.args[k]); };
    r = Type::Error;
    switch (e.bi) {
      case Bi::Print:
        r = Type::Void;
        if (argc(e, 1)) arg(0);
        break;
      case Bi::Input:
        r = Type::String;
        argc(e, 0);
        break;
      case Bi::Range:
        for (auto& a : e.args) valueExpr(*a);
        error(e.span, "range() can only be used as the iterable of a 'for' loop.",
              "Example: for i in range(10) { ... }");
        break;
      case Bi::Len:
        r = Type::Int;
        if (argc(e, 1)) {
          Type a = arg(0);
          if (a != Type::Error && a != Type::String && !isCompound(a) && a != Type::EmptyArray &&
              a != Type::EmptyBrace)
            error(e.args[0]->span, "len() needs a string or a collection, got " + q(typeName(a)) + ".");
          else if (isStruct(a))
            error(e.args[0]->span, "len() needs a string or a collection, got " + q(typeName(a)) + ".");
        }
        break;
      case Bi::Split:
        r = arrayOf(Type::String);
        if (argc(e, 1)) need(*e.args[0], arg(0), Type::String, "Argument of 'split'");
        break;
      case Bi::Join:
        r = Type::String;
        if (argc(e, 2)) {
          need(*e.args[0], arg(0), arrayOf(Type::String), "Argument 1 of 'join'");
          need(*e.args[1], arg(1), Type::String, "Argument 2 of 'join'");
        }
        break;
      case Bi::Push:
        r = Type::Void;
        if (argc(e, 2)) {
          Type a = arg(0), v = arg(1);
          if (a == Type::Error) break;
          if (isArray(a) || isHeap(a) || isDeque(a)) {
            if (v != Type::Error && !convertTo(*e.args[1], v, elemOf(a)))
              error(e.args[1]->span, "Cannot push a value of type " + q(typeName(v)) + " into " +
                                         collectionWord(a) + " of " + q(typeName(elemOf(a))) + ".",
                    convHint(v, elemOf(a)));
          } else {
            error(e.args[0]->span, "push() needs an array, heap or deque as its first argument, got " +
                                       q(typeName(a)) + ".");
          }
        }
        break;
      case Bi::Pop:
        if (argc(e, 1)) {
          Type a = arg(0);
          if (isArray(a) || isHeap(a) || isDeque(a)) r = elemOf(a);
          else if (a != Type::Error)
            error(e.args[0]->span, "pop() needs an array, heap or deque, got " + q(typeName(a)) + ".");
        }
        break;
      case Bi::Copy:
        if (argc(e, 1)) {
          Type a = arg(0);
          if (isCompound(a)) r = a;
          else if (a != Type::Error)
            error(e.args[0]->span, "copy() needs an array, map, set, heap, deque or struct, got " +
                                       q(typeName(a)) + ".");
        }
        break;
      case Bi::Make:
        if (argc(e, 2)) {
          need(*e.args[0], arg(0), Type::Int, "Argument 1 of 'make'");
          Type v = arg(1);
          if (v == Type::EmptyArray || v == Type::EmptyBrace)
            error(e.args[1]->span, "Cannot infer the element type of an empty literal.");
          else
            r = arrayOf(v);
        }
        break;
      case Bi::Sort:
        r = Type::Void;
        if (argc(e, 1)) {
          Type a = arg(0);
          if (a != Type::Error && !isArray(a))
            error(e.args[0]->span, "sort() needs an array, got " + q(typeName(a)) + ".");
          else if (isArray(a) && !(isOrdered(elemOf(a)) || elemOf(a) == Type::Bool))
            error(e.args[0]->span,
                  "sort() needs an array of int, float, string, char or bool, got " + q(typeName(a)) + ".");
        }
        break;
      case Bi::Min:
      case Bi::Max:
      case Bi::Pow:
        if (argc(e, 2)) {
          Type a = arg(0), b = arg(1);
          if (a == Type::Error || b == Type::Error) break;
          if (a == b && (a == Type::Int || a == Type::Float)) r = a;
          else
            error(e.span, q(n) + " needs two int or two float values, got " + q(typeName(a)) +
                              " and " + q(typeName(b)) + ".",
                  "Use float(value) or int(value) to convert explicitly.");
        }
        break;
      case Bi::Abs:
        if (argc(e, 1)) {
          Type a = arg(0);
          if (a == Type::Int || a == Type::Float) r = a;
          else if (a != Type::Error)
            error(e.args[0]->span, "abs() needs an int or a float, got " + q(typeName(a)) + ".");
        }
        break;
      case Bi::Sqrt:
      case Bi::Log:
      case Bi::Log2:
      case Bi::Log10:
      case Bi::Exp:
      case Bi::Sin:
      case Bi::Cos:
      case Bi::Tan: floatFn(e, r, 1, Type::Float); break;
      case Bi::Atan2: floatFn(e, r, 2, Type::Float); break;
      case Bi::Floor:
      case Bi::Ceil:
      case Bi::Round: floatFn(e, r, 1, Type::Int); break;
      case Bi::Gcd:
      case Bi::Lcm:
      case Bi::Powmod: {
        size_t cnt = e.bi == Bi::Powmod ? 3 : 2;
        r = Type::Int;
        if (argc(e, cnt))
          for (size_t k = 0; k < cnt; ++k) need(*e.args[k], arg(k), Type::Int, "Argument " + std::to_string(k + 1) + " of " + q(n));
        break;
      }
      case Bi::Has:
        r = Type::Bool;
        if (argc(e, 2)) {
          Type c = arg(0), k = arg(1);
          if (isMap(c) || isSet(c)) need(*e.args[1], k, keyOf(c), "The key");
          else if (isArray(c) && (isOrdered(elemOf(c)) || elemOf(c) == Type::Bool))
            need(*e.args[1], k, elemOf(c), "The value");
          else if (c != Type::Error)
            error(e.args[0]->span, "has() needs a map, set or array, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::Remove:
        r = Type::Void;
        if (argc(e, 2)) {
          Type c = arg(0), k = arg(1);
          if (isMap(c) || isSet(c)) need(*e.args[1], k, keyOf(c), "The key");
          else if (c != Type::Error)
            error(e.args[0]->span, "remove() needs a map or a set, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::Get:
        if (argc(e, 3)) {
          Type c = arg(0), k = arg(1), d = arg(2);
          if (isMap(c)) {
            need(*e.args[1], k, keyOf(c), "The key");
            need(*e.args[2], d, valueOf(c), "The default value");
            r = valueOf(c);
          } else if (c != Type::Error) {
            error(e.args[0]->span, "get() needs a map, got " + q(typeName(c)) + ".");
          }
        }
        break;
      case Bi::Keys:
      case Bi::Values:
        if (argc(e, 1)) {
          Type c = arg(0);
          if (isMap(c)) r = arrayOf(e.bi == Bi::Keys ? keyOf(c) : valueOf(c));
          else if (isSet(c) && e.bi == Bi::Keys) r = arrayOf(elemOf(c));
          else if (c != Type::Error)
            error(e.args[0]->span, n + "() needs a map" + (e.bi == Bi::Keys ? " or a set" : "") +
                                       ", got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::First:
      case Bi::Last:
        if (argc(e, 1)) {
          Type c = arg(0);
          if (isMap(c) || isSet(c)) r = keyOf(c);
          else if (c != Type::Error)
            error(e.args[0]->span, n + "() needs a map or a set, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::Add:
        r = Type::Void;
        if (argc(e, 2)) {
          Type c = arg(0), v = arg(1);
          if (isSet(c)) need(*e.args[1], v, elemOf(c), "The value");
          else if (c != Type::Error)
            error(e.args[0]->span, "add() needs a set, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::Top:
        if (argc(e, 1)) {
          Type c = arg(0);
          if (isHeap(c)) r = elemOf(c);
          else if (c != Type::Error)
            error(e.args[0]->span, "top() needs a heap, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::PushFront:
        r = Type::Void;
        if (argc(e, 2)) {
          Type c = arg(0), v = arg(1);
          if (isDeque(c)) need(*e.args[1], v, elemOf(c), "The value");
          else if (c != Type::Error)
            error(e.args[0]->span, "push_front() needs a deque, got " + q(typeName(c)) + ".");
        }
        break;
      case Bi::PopFront:
      case Bi::Front:
      case Bi::Back:
        if (argc(e, 1)) {
          Type c = arg(0);
          if (isDeque(c)) r = elemOf(c);
          else if (c != Type::Error)
            error(e.args[0]->span, n + "() needs a deque, got " + q(typeName(c)) + ".");
        }
        break;
      default: break;
    }
    return true;
  }

  Type computeConstruct(Expr& e) {
    Stmt* decl = structs.at(e.s);
    Type st = decl->declType;
    const auto& fields = info(st).fields;
    size_t n = fields.size();
    std::vector<bool> filled(n, false);
    e.fieldIdx.assign(e.args.size(), -1);
    size_t nextPos = 0;
    bool sawNamed = false;
    for (size_t k = 0; k < e.args.size(); ++k) {
      Type at = valueExpr(*e.args[k]);
      int fi = -1;
      bool named = k < e.argNames.size() && !e.argNames[k].empty();
      if (named) {
        sawNamed = true;
        for (size_t f = 0; f < n; ++f)
          if (fields[f].first == e.argNames[k]) fi = (int)f;
        if (fi < 0) {
          error(e.args[k]->span, "Struct " + q(e.s) + " has no field " + q(e.argNames[k]) + ".");
          continue;
        }
      } else {
        if (sawNamed) {
          error(e.args[k]->span, "Positional arguments must come before named arguments.");
          continue;
        }
        if (nextPos >= n) {
          error(e.args[k]->span, "Struct " + q(e.s) + " has only " + std::to_string(n) + " field(s).");
          continue;
        }
        fi = (int)nextPos++;
      }
      if (filled[(size_t)fi]) {
        error(e.args[k]->span, "Field " + q(fields[(size_t)fi].first) + " is given twice.");
        continue;
      }
      filled[(size_t)fi] = true;
      e.fieldIdx[k] = fi;
      Type ft = fields[(size_t)fi].second;
      if (at != Type::Error && !convertTo(*e.args[k], at, ft))
        error(e.args[k]->span, "Field " + q(fields[(size_t)fi].first) + " of " + q(e.s) +
                                   " must be " + q(typeName(ft)) + ", got " + q(typeName(at)) + ".",
              convHint(at, ft));
    }
    for (size_t f = 0; f < n; ++f)
      if (!filled[f]) {
        error(e.span, "Missing field " + q(fields[f].first) + " in " + e.s + "(...).");
        break;
      }
    e.bi = Bi::Construct;
    return st;
  }

  Type computeCall(Expr& e) {
    const std::string& n = e.s;
    size_t nargs = e.args.size();
    auto checkArgs = [&]() {
      for (auto& a : e.args) valueExpr(*a);
    };
    bool named = false;
    for (auto& a : e.argNames)
      if (!a.empty()) named = true;
    if (structs.count(n)) return computeConstruct(e);
    if (named) error(e.span, "Named arguments can only be used to create a struct.");
    Type br;
    if (!fns.count(n) && builtin(e, br)) return br;
    if (n == "int" || n == "float" || n == "string") {
      Type res = n == "int" ? Type::Int : n == "float" ? Type::Float : Type::String;
      e.bi = n == "int" ? Bi::ConvInt : n == "float" ? Bi::ConvFloat : Bi::ConvString;
      if (nargs != 1) {
        checkArgs();
        error(e.span, q(n) + " expects 1 argument but got " + std::to_string(nargs) + ".");
        return res;
      }
      Type a = valueExpr(*e.args[0]);
      if (a == Type::Error) return res;
      bool ok = res == Type::String ||
                (res == Type::Int && (a == Type::Int || a == Type::Float || a == Type::Char ||
                                      a == Type::String)) ||
                (res == Type::Float && (a == Type::Int || a == Type::Float || a == Type::String));
      if (!ok)
        error(e.args[0]->span,
              "Cannot convert a value of type " + q(typeName(a)) + " to " + q(n) + ".");
      return res;
    }
    if (n == "bool" || n == "char") {
      checkArgs();
      error(e.span, "There is no conversion function " + q(n) + " in v0.1.");
      return Type::Error;
    }
    auto it = fns.find(n);
    if (it == fns.end()) {
      checkArgs();
      std::set<std::string> names;
      for (auto& kv : fns) names.insert(kv.first);
      for (auto& kv : structs) names.insert(kv.first);
      error(e.span, "Undefined function " + q(n) + ".", suggest(n, names));
      return Type::Error;
    }
    const FnSig& sig = it->second;
    e.callee = sig.decl;
    if (nargs != sig.params.size()) {
      checkArgs();
      error(e.span, "Function " + q(n) + " expects " + std::to_string(sig.params.size()) +
                        " argument(s) but got " + std::to_string(nargs) + ".");
      return sig.ret;
    }
    for (size_t k = 0; k < nargs; ++k) {
      Type t = valueExpr(*e.args[k]);
      if (t != Type::Error && !convertTo(*e.args[k], t, sig.params[k]))
        error(e.args[k]->span, "Argument " + std::to_string(k + 1) + " of " + q(n) + " must be " +
                                   q(typeName(sig.params[k])) + ", got " + q(typeName(t)) + ".",
              convHint(t, sig.params[k]));
    }
    return sig.ret;
  }
};

}  // namespace

std::vector<Diagnostic> checkProgram(Program& program) {
  Checker c;
  return c.run(program);
}

}  // namespace ipx
