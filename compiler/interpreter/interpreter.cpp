#include "interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <istream>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>

namespace ipx {
namespace {

struct Value;

// Arrays, heaps, structs, maps, sets and deques are shared references (like Python objects);
// scalars and strings are copied.
struct Value {
  Type t = Type::Void;
  int64_t i = 0;
  double f = 0;
  std::string s;
  // One shared object: std::vector<Value> (array, heap, struct fields), MapStore or DequeStore.
  std::shared_ptr<void> obj;
  std::vector<Value>* vec() const { return static_cast<std::vector<Value>*>(obj.get()); }
};

struct KeyLess {
  bool operator()(const Value& a, const Value& b) const {
    return a.t == Type::String ? a.s < b.s : a.i < b.i;
  }
};
struct MapStore {
  std::map<Value, Value, KeyLess> m;
};
struct DequeStore {
  std::deque<Value> d;
};
MapStore& M(const Value& v) { return *static_cast<MapStore*>(v.obj.get()); }
DequeStore& D(const Value& v) { return *static_cast<DequeStore*>(v.obj.get()); }

Value mkInt(int64_t v) { Value r; r.t = Type::Int; r.i = v; return r; }
Value mkFloat(double v) { Value r; r.t = Type::Float; r.f = v; return r; }
Value mkStr(std::string v) { Value r; r.t = Type::String; r.s = std::move(v); return r; }
Value mkBool(bool v) { Value r; r.t = Type::Bool; r.i = v; return r; }
Value mkChar(int64_t v) { Value r; r.t = Type::Char; r.i = v; return r; }
Value mkArr(Type t, std::vector<Value> v) {
  Value r;
  r.t = t;
  r.obj = std::make_shared<std::vector<Value>>(std::move(v));
  return r;
}

// A new empty collection of static type t.
Value newEmpty(Type t) {
  Value r;
  r.t = t;
  if (!isCompound(t)) return r;
  switch (info(t).kind) {
    case Kind::Map:
    case Kind::Set: r.obj = std::make_shared<MapStore>(); break;
    case Kind::Deque: r.obj = std::make_shared<DequeStore>(); break;
    default: r.obj = std::make_shared<std::vector<Value>>(); break;
  }
  return r;
}

Value deepCopy(const Value& v, Type t) {
  if (!isCompound(t)) return v;
  const TypeInfo& ti = info(t);
  Value r = v;
  switch (ti.kind) {
    case Kind::Array:
    case Kind::Heap:
    case Kind::MaxHeap:
      if (!v.obj) return v;
      r.obj = std::make_shared<std::vector<Value>>();
      r.vec()->reserve(v.vec()->size());
      for (auto& x : *v.vec()) r.vec()->push_back(deepCopy(x, ti.a));
      break;
    case Kind::Struct:
      if (!v.obj) return v;
      r.obj = std::make_shared<std::vector<Value>>();
      for (size_t k = 0; k < v.vec()->size(); ++k)
        r.vec()->push_back(deepCopy((*v.vec())[k], ti.fields[k].second));
      break;
    case Kind::Deque: {
      if (!v.obj) return v;
      auto nd = std::make_shared<DequeStore>();
      for (auto& x : D(v).d) nd->d.push_back(deepCopy(x, ti.a));
      r.obj = nd;
      break;
    }
    case Kind::Set:
    case Kind::Map: {
      if (!v.obj) return v;
      auto ns = std::make_shared<MapStore>();
      for (auto& kv : M(v).m)
        ns->m.emplace(kv.first, ti.kind == Kind::Map ? deepCopy(kv.second, ti.b) : Value());
      r.obj = ns;
      break;
    }
  }
  return r;
}

std::string formatFloat(double d) {
  if (std::isnan(d)) return "nan";
  if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
  char buf[64];
  for (int p = 15; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*g", p, d);
    if (std::strtod(buf, nullptr) == d) break;
  }
  std::string r = buf;
  if (r.find_first_of(".e") == std::string::npos) r += ".0";
  return r;
}

std::string toStr(const Value& v) {  // scalars
  switch (v.t) {
    case Type::Int: return std::to_string(v.i);
    case Type::Float: return formatFloat(v.f);
    case Type::String: return v.s;
    case Type::Bool: return v.i ? "true" : "false";
    case Type::Char: return std::string(1, (char)v.i);
    default: return "";
  }
}

// Formats a value using its static type, so empty collections and nested values print correctly.
std::string fmt(const Value& v, Type t, bool nested) {
  if (t == Type::EmptyArray) return "[]";
  if (t == Type::EmptyBrace) return "{}";
  if (isCompound(t)) {
    const TypeInfo& ti = info(t);
    std::string r;
    switch (ti.kind) {
      case Kind::Array:
      case Kind::Heap:
      case Kind::MaxHeap:
        r = "[";
        if (v.obj)
          for (size_t k = 0; k < v.vec()->size(); ++k) r += (k ? ", " : "") + fmt((*v.vec())[k], ti.a, true);
        return r + "]";
      case Kind::Deque: {
        r = "[";
        size_t k = 0;
        if (v.obj)
          for (auto& x : D(v).d) r += (k++ ? ", " : "") + fmt(x, ti.a, true);
        return r + "]";
      }
      case Kind::Set: {
        r = "{";
        size_t k = 0;
        if (v.obj)
          for (auto& kv : M(v).m) r += (k++ ? ", " : "") + fmt(kv.first, ti.a, true);
        return r + "}";
      }
      case Kind::Map: {
        r = "{";
        size_t k = 0;
        if (v.obj)
          for (auto& kv : M(v).m)
            r += (k++ ? ", " : "") + fmt(kv.first, ti.a, true) + ": " + fmt(kv.second, ti.b, true);
        return r + "}";
      }
      case Kind::Struct:
        r = ti.name + "(";
        if (v.obj)
          for (size_t k = 0; k < v.vec()->size(); ++k)
            r += (k ? ", " : "") + ti.fields[k].first + " = " + fmt((*v.vec())[k], ti.fields[k].second, true);
        return r + ")";
    }
  }
  if (nested && t == Type::String) return "\"" + v.s + "\"";
  if (nested && t == Type::Char) return "'" + std::string(1, (char)v.i) + "'";
  return toStr(v);
}

template <class T>
bool compareOp(Tok op, const T& x, const T& y) {
  switch (op) {
    case Tok::Lt: return x < y;
    case Tok::Le: return x <= y;
    case Tok::Gt: return x > y;
    case Tok::Ge: return x >= y;
    case Tok::EqEq: return x == y;
    default: return x != y;
  }
}

bool lessFloat(const Value& x, const Value& y) {  // NaN sorts last
  bool nx = std::isnan(x.f), ny = std::isnan(y.f);
  if (nx || ny) return !nx && ny;
  return x.f < y.f;
}

bool valLess(const Value& a, const Value& b, Type el) {
  if (el == Type::String) return a.s < b.s;
  if (el == Type::Float) return lessFloat(a, b);
  return a.i < b.i;
}

struct Frame {
  std::vector<Value> slots;  // variables are resolved to slots by the checker
};
enum class Flow { Normal, Return, Break, Continue };

class Interp {
 public:
  Interp(std::ostream& o, const RunOptions& op) : out(o), opt(op) {}

  void run(Program& p) {
    for (auto& s : p.stmts)
      if (s->kind == StmtKind::Fn) fns[s->name] = s.get();
    globals.slots.resize((size_t)p.numGlobals);
    frame = &globals;
    for (auto& s : p.stmts)
      if (s->kind != StmtKind::Fn) exec(*s);
    auto m = fns.find("main");
    if (m != fns.end()) call(*m->second, {}, m->second->nameSpan);
  }

 private:
  std::ostream& out;
  RunOptions opt;
  std::map<std::string, Stmt*> fns;
  Frame globals;
  Frame* frame = nullptr;
  Value retVal;
  unsigned long long steps = 0;
  int depth = 0;

  [[noreturn]] void fail(const Span& sp, const std::string& msg, const std::string& hint = "") {
    Diagnostic d;
    d.message = msg;
    d.hint = hint;
    d.line = sp.line;
    d.col = sp.col;
    d.endCol = sp.endCol;
    d.runtime = true;
    throw DiagnosticError(d);
  }

  void tick(const Span& sp) {
    if (opt.maxSteps && ++steps > opt.maxSteps)
      fail(sp, "Execution step limit exceeded.", "The program may contain an infinite loop.");
  }

  Value& at(const Expr& e) { return (e.global ? globals : *frame).slots[(size_t)e.slot]; }

  // Avoids copying big strings when the operand is a plain variable.
  const Value& ref(Expr& e, Value& tmp) {
    if (e.kind == ExprKind::Var) {
      Value& v = at(e);
      if (e.global && v.t == Type::Void) fail(e.span, "'" + e.s + "' is used before it is initialized.");
      return v;
    }
    tmp = eval(e);
    return tmp;
  }

  void checkIndex(int64_t i, size_t n, const Span& sp) {
    if (i < 0 || (uint64_t)i >= n)
      fail(sp, "Index " + std::to_string(i) + " is out of bounds for length " + std::to_string(n) + ".",
           i < 0 ? "Negative indexes are not supported; the last element is at len(a) - 1." : "");
  }

  void checkLen(unsigned long long n, const Span& sp) {
    if (n > opt.maxArray)
      fail(sp, "Collection too large (limit is " + std::to_string(opt.maxArray) + " elements).");
  }

  Flow block(std::vector<StmtP>& body) {
    Flow f = Flow::Normal;
    for (auto& st : body) {
      f = exec(*st);
      if (f != Flow::Normal) break;
    }
    return f;
  }

  Flow runBody(Stmt& s, const Value& item) {  // one iteration with a fresh loop variable
    frame->slots[(size_t)s.slot] = item;
    Flow f = Flow::Normal;
    for (auto& st : s.body) {
      f = exec(*st);
      if (f != Flow::Normal) break;
    }
    return f;
  }

  Flow execFor(Stmt& s) {
    Expr& it = *s.expr;
    if (it.kind == ExprKind::Call && it.s == "range" && it.bi == Bi::Range) {
      int64_t a = 0, b = 0, step = 1;
      if (it.args.size() == 1) {
        b = eval(*it.args[0]).i;
      } else {
        a = eval(*it.args[0]).i;
        b = eval(*it.args[1]).i;
        if (it.args.size() == 3) step = eval(*it.args[2]).i;
      }
      if (step == 0) fail(it.span, "range() step cannot be zero.");
      for (int64_t i = a; step > 0 ? i < b : i > b;) {
        tick(s.span);
        Flow f = runBody(s, mkInt(i));
        if (f == Flow::Return) return f;
        if (f == Flow::Break) break;
        if (__builtin_add_overflow(i, step, &i)) break;
      }
      return Flow::Normal;
    }
    Value coll = eval(it);
    Type ct = it.type;
    if (isMap(ct) || isSet(ct)) {  // iterates over a snapshot of the keys, in order
      std::vector<Value> keys;
      for (auto& kv : M(coll).m) keys.push_back(kv.first);
      for (auto& k : keys) {
        tick(s.span);
        Flow f = runBody(s, k);
        if (f == Flow::Return) return f;
        if (f == Flow::Break) break;
      }
      return Flow::Normal;
    }
    for (size_t k = 0;; ++k) {
      Value item;
      if (isDeque(ct)) {
        auto& d = D(coll).d;
        if (k >= d.size()) break;
        item = d[k];
      } else if (coll.obj) {
        if (k >= coll.vec()->size()) break;
        item = (*coll.vec())[k];
      } else {
        if (k >= coll.s.size()) break;
        item = mkChar((unsigned char)coll.s[k]);
      }
      tick(s.span);
      Flow f = runBody(s, item);
      if (f == Flow::Return) return f;
      if (f == Flow::Break) break;
    }
    return Flow::Normal;
  }

  Flow execAssignTarget(Stmt& s) {
    Value v = eval(*s.expr);
    Expr& tg = *s.target;
    if (tg.kind == ExprKind::Field) {
      Value tmp;
      const Value& b = ref(*tg.args[0], tmp);
      (*b.vec())[(size_t)tg.slot] = std::move(v);
      return Flow::Normal;
    }
    if (tg.slot == 1) {
      Value key = eval(*tg.args[1]);
      Value tmp;
      const Value& b = ref(*tg.args[0], tmp);
      auto& m = M(b).m;
      if (!m.count(key)) checkLen(m.size() + 1, tg.span);
      m[key] = std::move(v);
      return Flow::Normal;
    }
    int64_t i = eval(*tg.args[1]).i;
    Value tmp;
    const Value& b = ref(*tg.args[0], tmp);
    if (tg.slot == 2) {
      auto& d = D(b).d;
      checkIndex(i, d.size(), tg.span);
      d[(size_t)i] = std::move(v);
    } else {
      checkIndex(i, b.vec()->size(), tg.span);
      (*b.vec())[(size_t)i] = std::move(v);
    }
    return Flow::Normal;
  }

  [[gnu::noinline]] Flow execMatch(Stmt& s) {
    Value subject = eval(*s.expr);
    for (auto& arm : s.arms)
      for (auto& v : arm.values) {
        Value x = eval(*v);
        if (subject.t == Type::String ? subject.s == x.s : subject.i == x.i) return block(arm.body);
      }
    if (s.hasElse) return block(s.elseBody);
    return Flow::Normal;
  }

  Flow exec(Stmt& s) {
    tick(s.span);
    switch (s.kind) {
      case StmtKind::VarDecl:
      case StmtKind::Assign: {
        Value v = eval(*s.expr);
        frame->slots[(size_t)s.slot] = std::move(v);
        return Flow::Normal;
      }
      case StmtKind::IndexAssign: return execAssignTarget(s);
      case StmtKind::ExprStmt: eval(*s.expr); return Flow::Normal;
      case StmtKind::If:
        if (eval(*s.expr).i) return block(s.body);
        if (s.hasElse) return block(s.elseBody);
        return Flow::Normal;
      case StmtKind::While:
        while (eval(*s.expr).i) {
          tick(s.span);
          Flow f = block(s.body);
          if (f == Flow::Return) return f;
          if (f == Flow::Break) break;
        }
        return Flow::Normal;
      case StmtKind::For: return execFor(s);
      case StmtKind::DoWhile:
        for (;;) {
          tick(s.span);
          Flow f = block(s.body);
          if (f == Flow::Return) return f;
          if (f == Flow::Break) break;
          if (!eval(*s.expr).i) break;
        }
        return Flow::Normal;
      case StmtKind::Match: return execMatch(s);
      case StmtKind::Break: return Flow::Break;
      case StmtKind::Continue: return Flow::Continue;
      case StmtKind::Return:
        retVal = s.expr ? eval(*s.expr) : Value();
        return Flow::Return;
      case StmtKind::Fn:
      case StmtKind::StructDecl: return Flow::Normal;
    }
    return Flow::Normal;
  }

  Value call(Stmt& fn, std::vector<Value> args, const Span& sp) {
    if (depth >= opt.maxDepth)
      fail(sp, "Maximum call depth (" + std::to_string(opt.maxDepth) + ") exceeded.",
           "Check for infinite recursion.");
    Frame f;
    f.slots.resize((size_t)fn.numSlots);
    for (size_t k = 0; k < fn.params.size(); ++k) f.slots[(size_t)fn.params[k].slot] = std::move(args[k]);
    Frame* saved = frame;
    frame = &f;
    ++depth;
    Flow fl = Flow::Normal;
    for (auto& st : fn.body) {
      fl = exec(*st);
      if (fl == Flow::Return) break;
    }
    frame = saved;
    --depth;
    Value r = fl == Flow::Return ? std::move(retVal) : Value();
    retVal = Value();
    return r;
  }

  Value eval(Expr& e) {
    switch (e.kind) {
      case ExprKind::Int: return mkInt(e.i);
      case ExprKind::Float: return mkFloat(e.f);
      case ExprKind::String: return mkStr(e.s);
      case ExprKind::Bool: return mkBool(e.i != 0);
      case ExprKind::Char: return mkChar(e.i);
      case ExprKind::Var: {
        const Value& v = at(e);
        if (e.global && v.t == Type::Void) fail(e.span, "'" + e.s + "' is used before it is initialized.");
        return v;
      }
      case ExprKind::Unary: return evalUnary(e);
      case ExprKind::Binary: return evalBinary(e);
      case ExprKind::Call: return evalCall(e);
      case ExprKind::Ternary: return eval(*e.args[0]).i ? eval(*e.args[1]) : eval(*e.args[2]);
      default: return evalOther(e);
    }
    return Value();
  }

  [[gnu::noinline]] Value evalOther(Expr& e) {
    switch (e.kind) {
      case ExprKind::Array: {
        std::vector<Value> v;
        v.reserve(e.args.size());
        for (auto& a : e.args) v.push_back(eval(*a));
        return mkArr(e.type, std::move(v));
      }
      case ExprKind::SetLit: {
        Value r = newEmpty(e.type);
        if (isSet(e.type))
          for (auto& a : e.args) M(r).m[eval(*a)] = Value();
        return r;
      }
      case ExprKind::MapLit: {
        Value r = newEmpty(e.type);
        for (size_t k = 0; k + 1 < e.args.size(); k += 2) {
          Value key = eval(*e.args[k]);
          M(r).m[key] = eval(*e.args[k + 1]);
        }
        return r;
      }
      case ExprKind::Field: {
        Value tmp;
        const Value& b = ref(*e.args[0], tmp);
        return (*b.vec())[(size_t)e.slot];
      }
      case ExprKind::Index: {
        Type bt = e.args[0]->type;
        if (e.slot == 1) {
          Value key = eval(*e.args[1]);
          Value tmp;
          const Value& b = ref(*e.args[0], tmp);
          auto& m = M(b).m;
          auto it = m.find(key);
          if (it == m.end())
            fail(e.span, "Key not found: " + fmt(key, keyOf(bt), true) + ".",
                 "Use get(map, key, default) or has(map, key) for keys that may be missing.");
          return it->second;
        }
        int64_t i = eval(*e.args[1]).i;
        Value tmp;
        const Value& b = ref(*e.args[0], tmp);
        if (e.slot == 2) {
          auto& d = D(b).d;
          checkIndex(i, d.size(), e.span);
          return d[(size_t)i];
        }
        checkIndex(i, b.obj ? b.vec()->size() : b.s.size(), e.span);
        return b.obj ? (*b.vec())[(size_t)i] : mkChar((unsigned char)b.s[(size_t)i]);
      }
      default: break;
    }
    return Value();
  }

  Value evalUnary(Expr& e) {
    Value v = eval(*e.args[0]);
    if (e.op == Tok::Bang) return mkBool(!v.i);
    if (e.op == Tok::Tilde) return mkInt(~v.i);
    if (v.t == Type::Float) return mkFloat(-v.f);
    if (v.i == INT64_MIN) fail(e.span, "Integer overflow in negation.");
    return mkInt(-v.i);
  }

  // Integer arithmetic and bit operations (checked: overflow and division by zero are errors).
  int64_t intOp(const Expr& e, int64_t a, int64_t b) {
    int64_t r = 0;
    switch (e.op) {
      case Tok::Plus:
        if (__builtin_add_overflow(a, b, &r)) fail(e.span, "Integer overflow in addition.");
        break;
      case Tok::Minus:
        if (__builtin_sub_overflow(a, b, &r)) fail(e.span, "Integer overflow in subtraction.");
        break;
      case Tok::Star:
        if (__builtin_mul_overflow(a, b, &r)) fail(e.span, "Integer overflow in multiplication.");
        break;
      case Tok::Amp: r = a & b; break;
      case Tok::Pipe: r = a | b; break;
      case Tok::Caret: r = a ^ b; break;
      case Tok::Shr:
      case Tok::Shl:
        if (b < 0 || b > 63) fail(e.span, "Shift count must be between 0 and 63.");
        if (e.op == Tok::Shr) {
          r = a >> b;
        } else {
          r = (int64_t)((uint64_t)a << b);
          if ((r >> b) != a) fail(e.span, "Integer overflow in shift.");
        }
        break;
      default:
        if (b == 0) fail(e.span, "Division by zero.");
        if (a == INT64_MIN && b == -1) fail(e.span, "Integer overflow in division.");
        r = e.op == Tok::Slash ? a / b : a % b;
    }
    return r;
  }

  // Fast path: an expression the checker typed as int is evaluated on raw int64 values,
  // without building Value objects for every intermediate result.
  int64_t evalInt(Expr& e) {
    switch (e.kind) {
      case ExprKind::Int: return e.i;
      case ExprKind::Var: {
        const Value& v = at(e);
        if (e.global && v.t == Type::Void) fail(e.span, "'" + e.s + "' is used before it is initialized.");
        return v.i;
      }
      case ExprKind::Binary:
        if (e.type == Type::Int && e.args[0]->type == Type::Int && e.args[1]->type == Type::Int) {
          int64_t l = evalInt(*e.args[0]);
          int64_t r = evalInt(*e.args[1]);
          return intOp(e, l, r);
        }
        return eval(e).i;
      default: return eval(e).i;
    }
  }

  Value evalIntBinary(Expr& e) {
    int64_t l = evalInt(*e.args[0]);
    int64_t r = evalInt(*e.args[1]);
    switch (e.op) {
      case Tok::Lt: return mkBool(l < r);
      case Tok::Le: return mkBool(l <= r);
      case Tok::Gt: return mkBool(l > r);
      case Tok::Ge: return mkBool(l >= r);
      case Tok::EqEq: return mkBool(l == r);
      case Tok::NotEq: return mkBool(l != r);
      default: return mkInt(intOp(e, l, r));
    }
  }

  Value evalBinary(Expr& e) {
    if (e.args[0]->type == Type::Int && e.args[1]->type == Type::Int) return evalIntBinary(e);
    if (e.op == Tok::AndAnd) {
      if (!eval(*e.args[0]).i) return mkBool(false);
      return mkBool(eval(*e.args[1]).i != 0);
    }
    if (e.op == Tok::OrOr) {
      if (eval(*e.args[0]).i) return mkBool(true);
      return mkBool(eval(*e.args[1]).i != 0);
    }
    Value a = eval(*e.args[0]);
    Value b = eval(*e.args[1]);
    switch (e.op) {
      case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge: case Tok::EqEq: case Tok::NotEq:
        if (a.t == Type::String) return mkBool(compareOp(e.op, a.s, b.s));
        if (a.t == Type::Float) return mkBool(compareOp(e.op, a.f, b.f));
        return mkBool(compareOp(e.op, a.i, b.i));
      default: break;
    }
    if (a.t == Type::String) {  // only '+' type-checks for strings
      if (a.s.size() + b.s.size() > 10000000)
        fail(e.span, "String too long (limit is 10 million characters).");
      return mkStr(a.s + b.s);
    }
    if (a.t == Type::Float) {
      switch (e.op) {
        case Tok::Plus: return mkFloat(a.f + b.f);
        case Tok::Minus: return mkFloat(a.f - b.f);
        case Tok::Star: return mkFloat(a.f * b.f);
        default:
          if (b.f == 0) fail(e.span, "Division by zero.");
          return mkFloat(a.f / b.f);
      }
    }
    return mkInt(intOp(e, a.i, b.i));
  }

  Value evalCall(Expr& e) {
    if (e.callee) {
      std::vector<Value> args;
      args.reserve(e.args.size());
      for (auto& a : e.args) args.push_back(eval(*a));
      return call(*e.callee, std::move(args), e.span);
    }
    switch (e.bi) {
      case Bi::ConvString: return mkStr(fmt(eval(*e.args[0]), e.args[0]->type, false));
      case Bi::ConvInt: return toInt(eval(*e.args[0]), e.span);
      case Bi::ConvFloat: return toFloat(eval(*e.args[0]), e.span);
      default: return builtin(e);
    }
  }

  static uint64_t uabs(int64_t v) { return v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v; }

  int64_t floatToInt(double d, const Expr& e) {
    if (std::isnan(d) || d >= 9223372036854775808.0 || d < -9223372036854775808.0)
      fail(e.span, e.s + "() result is out of the int range.");
    return (int64_t)d;
  }

  Value builtin(Expr& e) {
    auto A = [&](size_t k) { return eval(*e.args[k]); };
    switch (e.bi) {
      case Bi::Print:
        out << fmt(A(0), e.args[0]->type, false) << "\n";
        return Value();
      case Bi::Input: {
        std::string line;
        if (!opt.in) fail(e.span, "input() is not available here.");
        if (!std::getline(*opt.in, line)) fail(e.span, "No more input to read.");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return mkStr(std::move(line));
      }
      case Bi::Len: {
        Value tmp;
        const Value& v = ref(*e.args[0], tmp);
        Type t = e.args[0]->type;
        if (isMap(t) || isSet(t)) return mkInt((int64_t)M(v).m.size());
        if (isDeque(t)) return mkInt((int64_t)D(v).d.size());
        return mkInt((int64_t)(v.obj ? v.vec()->size() : v.s.size()));
      }
      case Bi::Split: {
        Value v = A(0);
        std::vector<Value> parts;
        size_t p = 0;
        while (p < v.s.size()) {
          while (p < v.s.size() && std::isspace((unsigned char)v.s[p])) ++p;
          size_t st = p;
          while (p < v.s.size() && !std::isspace((unsigned char)v.s[p])) ++p;
          if (p > st) parts.push_back(mkStr(v.s.substr(st, p - st)));
        }
        return mkArr(e.type, std::move(parts));
      }
      case Bi::Join: {
        Value a = A(0), sep = A(1);
        std::string r;
        for (size_t k = 0; k < a.vec()->size(); ++k) {
          if (k) r += sep.s;
          r += (*a.vec())[k].s;
          if (r.size() > 10000000) fail(e.span, "String too long (limit is 10 million characters).");
        }
        return mkStr(std::move(r));
      }
      case Bi::Push: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Type ct = e.args[0]->type;
        Value v = A(1);
        if (isDeque(ct)) {
          checkLen(D(c).d.size() + 1, e.span);
          D(c).d.push_back(std::move(v));
        } else {
          checkLen(c.vec()->size() + 1, e.span);
          c.vec()->push_back(std::move(v));
          if (isHeap(ct)) {
            Type el = elemOf(ct);
            bool mx = isMaxHeap(ct);
            std::push_heap(c.vec()->begin(), c.vec()->end(), [&](const Value& x, const Value& y) {
              return mx ? valLess(x, y, el) : valLess(y, x, el);
            });
          }
        }
        return Value();
      }
      case Bi::Pop: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Type ct = e.args[0]->type;
        if (isDeque(ct)) {
          auto& d = D(c).d;
          if (d.empty()) fail(e.span, "pop() on an empty deque.");
          Value r = std::move(d.back());
          d.pop_back();
          return r;
        }
        if (c.vec()->empty()) fail(e.span, isHeap(ct) ? "pop() on an empty heap." : "pop() on an empty array.");
        if (isHeap(ct)) {
          Type el = elemOf(ct);
          bool mx = isMaxHeap(ct);
          std::pop_heap(c.vec()->begin(), c.vec()->end(), [&](const Value& x, const Value& y) {
            return mx ? valLess(x, y, el) : valLess(y, x, el);
          });
        }
        Value r = std::move(c.vec()->back());
        c.vec()->pop_back();
        return r;
      }
      case Bi::Top: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        if (c.vec()->empty()) fail(e.span, "top() on an empty heap.");
        return c.vec()->front();
      }
      case Bi::PushFront: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Value v = A(1);
        checkLen(D(c).d.size() + 1, e.span);
        D(c).d.push_front(std::move(v));
        return Value();
      }
      case Bi::PopFront:
      case Bi::Front:
      case Bi::Back: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        auto& d = D(c).d;
        if (d.empty()) fail(e.span, e.s + "() on an empty deque.");
        if (e.bi == Bi::Back) return d.back();
        Value r = d.front();
        if (e.bi == Bi::PopFront) d.pop_front();
        return r;
      }
      case Bi::Copy: return deepCopy(A(0), e.args[0]->type);
      case Bi::Make: {
        int64_t cnt = A(0).i;
        Value v = A(1);
        Type vt = e.args[1]->type;
        if (cnt < 0) fail(e.span, "make() needs a non-negative length.");
        checkLen((unsigned long long)cnt, e.span);
        std::vector<Value> items;
        items.reserve((size_t)cnt);
        for (int64_t k = 0; k < cnt; ++k) items.push_back(isCompound(vt) ? deepCopy(v, vt) : v);
        return mkArr(e.type, std::move(items));
      }
      case Bi::Sort: {
        Value a = A(0);
        Type el = elemOf(e.args[0]->type);
        std::sort(a.vec()->begin(), a.vec()->end(),
                  [el](const Value& x, const Value& y) { return valLess(x, y, el); });
        return Value();
      }
      case Bi::Has: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Type ct = e.args[0]->type;
        Value k = A(1);
        if (isMap(ct) || isSet(ct)) return mkBool(M(c).m.count(k) > 0);
        Type el = elemOf(ct);
        for (auto& x : *c.vec())
          if (el == Type::String ? x.s == k.s : el == Type::Float ? x.f == k.f : x.i == k.i)
            return mkBool(true);
        return mkBool(false);
      }
      case Bi::Remove: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        M(c).m.erase(A(1));
        return Value();
      }
      case Bi::Add: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Value k = A(1);
        if (!M(c).m.count(k)) checkLen(M(c).m.size() + 1, e.span);
        M(c).m.emplace(std::move(k), Value());
        return Value();
      }
      case Bi::Get: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        Value k = A(1);
        Value d = A(2);
        auto it = M(c).m.find(k);
        return it == M(c).m.end() ? d : it->second;
      }
      case Bi::Keys:
      case Bi::Values: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        std::vector<Value> items;
        for (auto& kv : M(c).m) items.push_back(e.bi == Bi::Keys ? kv.first : kv.second);
        return mkArr(e.type, std::move(items));
      }
      case Bi::First:
      case Bi::Last: {
        Value tmp;
        const Value& c = ref(*e.args[0], tmp);
        auto& m = M(c).m;
        if (m.empty()) fail(e.span, e.s + "() on an empty collection.");
        return e.bi == Bi::First ? m.begin()->first : m.rbegin()->first;
      }
      case Bi::Min:
      case Bi::Max: {
        Value a = A(0), b = A(1);
        bool less = a.t == Type::Float ? a.f < b.f : a.i < b.i;
        return (e.bi == Bi::Min) == less ? a : b;
      }
      case Bi::Abs: {
        Value a = A(0);
        if (a.t == Type::Float) return mkFloat(std::fabs(a.f));
        if (a.i == INT64_MIN) fail(e.span, "Integer overflow in abs().");
        return mkInt(a.i < 0 ? -a.i : a.i);
      }
      case Bi::Pow: {
        Value a = A(0), b = A(1);
        if (a.t == Type::Float) return mkFloat(std::pow(a.f, b.f));
        if (b.i < 0) fail(e.span, "pow() needs a non-negative exponent for int values.");
        int64_t base = a.i, ex = b.i, r = 1;
        while (ex > 0) {
          if ((ex & 1) && __builtin_mul_overflow(r, base, &r)) fail(e.span, "Integer overflow in pow().");
          ex >>= 1;
          if (ex && __builtin_mul_overflow(base, base, &base)) fail(e.span, "Integer overflow in pow().");
        }
        return mkInt(r);
      }
      case Bi::Powmod: {
        Value a = A(0), b = A(1), m = A(2);
        if (m.i <= 0) fail(e.span, "powmod() needs a positive modulus.");
        if (b.i < 0) fail(e.span, "powmod() needs a non-negative exponent.");
        __int128 mod = m.i, base = a.i % mod, r = 1 % mod;
        if (base < 0) base += mod;
        for (int64_t ex = b.i; ex > 0; ex >>= 1) {
          if (ex & 1) r = r * base % mod;
          base = base * base % mod;
        }
        return mkInt((int64_t)r);
      }
      case Bi::Gcd:
      case Bi::Lcm: {
        Value a = A(0), b = A(1);
        uint64_t ua = uabs(a.i), ub = uabs(b.i), g = std::gcd(ua, ub);
        if (e.bi == Bi::Gcd) {
          if (g > (uint64_t)INT64_MAX) fail(e.span, "Integer overflow in gcd().");
          return mkInt((int64_t)g);
        }
        if (ua == 0 || ub == 0) return mkInt(0);
        unsigned __int128 l = (unsigned __int128)(ua / g) * ub;
        if (l > (unsigned __int128)INT64_MAX) fail(e.span, "Integer overflow in lcm().");
        return mkInt((int64_t)l);
      }
      case Bi::Sqrt: {
        Value a = A(0);
        if (a.f < 0) fail(e.span, "sqrt() of a negative number.");
        return mkFloat(std::sqrt(a.f));
      }
      case Bi::Floor: return mkInt(floatToInt(std::floor(A(0).f), e));
      case Bi::Ceil: return mkInt(floatToInt(std::ceil(A(0).f), e));
      case Bi::Round: return mkInt(floatToInt(std::round(A(0).f), e));
      case Bi::Log:
      case Bi::Log2:
      case Bi::Log10: {
        Value a = A(0);
        if (a.f <= 0) fail(e.span, e.s + "() of a non-positive number.");
        return mkFloat(e.bi == Bi::Log ? std::log(a.f) : e.bi == Bi::Log2 ? std::log2(a.f) : std::log10(a.f));
      }
      case Bi::Exp: return mkFloat(std::exp(A(0).f));
      case Bi::Sin: return mkFloat(std::sin(A(0).f));
      case Bi::Cos: return mkFloat(std::cos(A(0).f));
      case Bi::Tan: return mkFloat(std::tan(A(0).f));
      case Bi::Atan2: {
        Value y = A(0), x = A(1);
        return mkFloat(std::atan2(y.f, x.f));
      }
      case Bi::Construct: {
        std::vector<Value> fields(info(e.type).fields.size());
        for (size_t k = 0; k < e.args.size(); ++k) fields[(size_t)e.fieldIdx[k]] = eval(*e.args[k]);
        return mkArr(e.type, std::move(fields));
      }
      default: break;
    }
    fail(e.span, "Unknown function '" + e.s + "'.");
  }

  Value toInt(const Value& v, const Span& sp) {
    switch (v.t) {
      case Type::Int: return v;
      case Type::Char: return mkInt(v.i);
      case Type::Float:
        if (std::isnan(v.f) || v.f >= 9223372036854775808.0 || v.f < -9223372036854775808.0)
          fail(sp, "Cannot convert float " + formatFloat(v.f) + " to int: value out of range.");
        return mkInt((int64_t)v.f);
      default: {
        errno = 0;
        char* end = nullptr;
        long long r = std::strtoll(v.s.c_str(), &end, 10);
        if (v.s.empty() || *end != '\0' || errno == ERANGE)
          fail(sp, "Cannot convert string \"" + v.s + "\" to int.");
        return mkInt(r);
      }
    }
  }

  Value toFloat(const Value& v, const Span& sp) {
    if (v.t == Type::Int) return mkFloat((double)v.i);
    if (v.t == Type::Float) return v;
    char* end = nullptr;
    double r = std::strtod(v.s.c_str(), &end);
    if (v.s.empty() || *end != '\0')
      fail(sp, "Cannot convert string \"" + v.s + "\" to float.");
    return mkFloat(r);
  }
};

}  // namespace

void interpret(Program& program, std::ostream& out, const RunOptions& opts) {
  Interp it(out, opts);
  it.run(program);
}

}  // namespace ipx
