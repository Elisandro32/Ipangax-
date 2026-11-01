#include "parser.hpp"

#include <algorithm>
#include <map>

namespace ipx {
namespace {

[[noreturn]] void fail(const std::string& msg, const Token& t, const std::string& hint = "") {
  Diagnostic d;
  d.message = msg;
  d.hint = hint;
  d.line = t.line;
  d.col = t.col;
  d.endCol = t.endCol;
  throw DiagnosticError(d);
}

bool isTypeTok(Tok k) {
  return k == Tok::TInt || k == Tok::TFloat || k == Tok::TString || k == Tok::TBool ||
         k == Tok::TChar;
}

Type toType(Tok k) {
  switch (k) {
    case Tok::TInt: return Type::Int;
    case Tok::TFloat: return Type::Float;
    case Tok::TString: return Type::String;
    case Tok::TBool: return Type::Bool;
    default: return Type::Char;
  }
}

bool compoundInfo(Tok k, Tok& bin, const char*& text) {
  switch (k) {
    case Tok::PlusEq: bin = Tok::Plus; text = "+"; return true;
    case Tok::MinusEq: bin = Tok::Minus; text = "-"; return true;
    case Tok::StarEq: bin = Tok::Star; text = "*"; return true;
    case Tok::SlashEq: bin = Tok::Slash; text = "/"; return true;
    case Tok::PercentEq: bin = Tok::Percent; text = "%"; return true;
    case Tok::AmpEq: bin = Tok::Amp; text = "&"; return true;
    case Tok::PipeEq: bin = Tok::Pipe; text = "|"; return true;
    case Tok::CaretEq: bin = Tok::Caret; text = "^"; return true;
    case Tok::ShlEq: bin = Tok::Shl; text = "<<"; return true;
    case Tok::ShrEq: bin = Tok::Shr; text = ">>"; return true;
    default: return false;
  }
}

bool isCompound(Tok k) {
  Tok b;
  const char* t;
  return compoundInfo(k, b, t);
}

bool isCollectionName(const std::string& n) {
  return n == "map" || n == "set" || n == "heap" || n == "maxheap" || n == "deque";
}

bool hasCall(const Expr& e) {
  if (e.kind == ExprKind::Call) return true;
  for (auto& a : e.args)
    if (hasCall(*a)) return true;
  return false;
}

Span joinSpan(const Span& a, const Span& b) {
  return {a.line, a.col, b.line == a.line ? b.endCol : a.endCol};
}

class Parser {
 public:
  explicit Parser(const std::vector<Token>& t) : toks(t) {
    // Struct names are known before parsing, so they can be used as types anywhere.
    for (size_t k = 0; k + 1 < toks.size(); ++k)
      if (toks[k].kind == Tok::KwStruct && toks[k + 1].kind == Tok::Ident &&
          !structs.count(toks[k + 1].text))
        structs[toks[k + 1].text] = newStruct(toks[k + 1].text);
  }

  Program parseProgram() {
    Program p;
    skipNl();
    while (!is(Tok::End)) {
      if (is(Tok::KwFn)) p.stmts.push_back(parseFn());
      else if (is(Tok::KwStruct)) p.stmts.push_back(parseStruct());
      else p.stmts.push_back(parseStatement());
      skipNl();
    }
    return p;
  }

 private:
  const std::vector<Token>& toks;
  size_t pos = 0;
  std::map<std::string, Type> structs;

  const Token& cur() const { return toks[pos]; }
  const Token& peek(size_t k) const { return toks[std::min(pos + k, toks.size() - 1)]; }
  const Token& prev() const { return toks[pos > 0 ? pos - 1 : 0]; }
  bool is(Tok k) const { return cur().kind == k; }
  Token next() {
    Token t = toks[pos];
    if (pos + 1 < toks.size()) ++pos;
    return t;
  }
  bool match(Tok k) {
    if (!is(k)) return false;
    next();
    return true;
  }
  Token expect(Tok k, const std::string& what) {
    if (!is(k)) fail("Expected " + what + " but found " + describe(cur()) + ".", cur());
    return next();
  }
  // Does a type start at offset k? Advances k past it. (map[K, V], set[T], T[], struct names...)
  bool scanType(size_t& k) const {
    const Token& t = peek(k);
    if (isTypeTok(t.kind)) {
      ++k;
    } else if (t.kind == Tok::Ident && peek(k + 1).kind == Tok::LBracket && isCollectionName(t.text)) {
      size_t j = k + 2;
      if (!scanType(j)) return false;
      if (t.text == "map") {
        if (peek(j).kind != Tok::Comma) return false;
        ++j;
        if (!scanType(j)) return false;
      }
      if (peek(j).kind != Tok::RBracket) return false;
      k = j + 1;
    } else if (t.kind == Tok::Ident && structs.count(t.text)) {
      ++k;
    } else {
      return false;
    }
    while (peek(k).kind == Tok::LBracket && peek(k + 1).kind == Tok::RBracket) k += 2;
    return true;
  }
  bool typeDeclAhead() const {  // "type name" starts here
    size_t k = 0;
    return scanType(k) && peek(k).kind == Tok::Ident;
  }
  Type parseType() {
    Token t = next();
    Type ty = Type::Error;
    if (isTypeTok(t.kind)) {
      ty = toType(t.kind);
    } else if (isCollectionName(t.text) && is(Tok::LBracket)) {
      next();
      Type a = parseType();
      Type b = Type::Error;
      if (t.text == "map") {
        expect(Tok::Comma, "','");
        b = parseType();
      }
      expect(Tok::RBracket, "']'");
      ty = t.text == "map"     ? mapOf(a, b)
           : t.text == "set"   ? setOf(a)
           : t.text == "heap"  ? heapOf(a, false)
           : t.text == "maxheap" ? heapOf(a, true)
                               : dequeOf(a);
    } else {
      ty = structs.at(t.text);
    }
    while (is(Tok::LBracket) && peek(1).kind == Tok::RBracket) {
      next();
      next();
      ty = arrayOf(ty);
    }
    return ty;
  }
  void skipNl() {
    while (is(Tok::Newline)) next();
  }
  Span spanFrom(const Token& a) const {
    const Token& p = prev();
    return {a.line, a.col, p.line == a.line ? p.endCol : a.endCol};
  }

  // x += e  becomes  x = x + e  (the target is cloned, so it must not contain calls)
  ExprP compoundValue(const Token& op, ExprP target, ExprP rhs) {
    Tok bin = Tok::Plus;
    const char* text = "+";
    compoundInfo(op.kind, bin, text);
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Binary;
    e->op = bin;
    e->s = text;
    e->span = joinSpan(target->span, rhs->span);
    e->args.push_back(std::move(target));
    e->args.push_back(std::move(rhs));
    return e;
  }

  StmtP parseCompoundVar() {
    auto s = mk(StmtKind::Assign);
    Token nm = next();
    Token op = next();
    ExprP rhs = parseExpr();
    auto var = std::make_unique<Expr>();
    var->kind = ExprKind::Var;
    var->s = nm.text;
    var->span = {nm.line, nm.col, nm.endCol};
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    s->expr = compoundValue(op, std::move(var), std::move(rhs));
    s->span = spanFrom(nm);
    return s;
  }

  StmtP mk(StmtKind k) {
    auto s = std::make_unique<Stmt>();
    s->kind = k;
    return s;
  }

  StmtP parseStatement() {
    StmtP s = parseStatementInner();
    if (!is(Tok::Newline) && !is(Tok::RBrace) && !is(Tok::End))
      fail("Unexpected " + describe(cur()) + " after statement.", cur(),
           "Separate statements with a new line.");
    return s;
  }

  StmtP parseStatementInner() {
    const Token& t = cur();
    switch (t.kind) {
      case Tok::KwIf: return parseIf();
      case Tok::KwWhile: return parseWhile();
      case Tok::KwReturn: return parseReturn();
      case Tok::KwFor: return parseFor();
      case Tok::KwMatch: return parseMatch();
      case Tok::KwDo: return parseDo();
      case Tok::KwBreak:
      case Tok::KwContinue: {
        auto b = mk(t.kind == Tok::KwBreak ? StmtKind::Break : StmtKind::Continue);
        Token kw = next();
        b->span = {kw.line, kw.col, kw.endCol};
        return b;
      }
      case Tok::KwConst: return parseDecl(true);
      case Tok::KwFn:
        fail("Functions can only be declared at the top level in v0.1.", t);
      case Tok::KwStruct:
        fail("Structs can only be declared at the top level.", t);
      case Tok::KwElse:
        fail("'else' must be on the same line as the closing '}' of the 'if' block.", t,
             "Write: } else {");
      case Tok::TInt: case Tok::TFloat: case Tok::TString: case Tok::TBool: case Tok::TChar:
        if (typeDeclAhead()) return parseDecl(false);
        break;
      case Tok::Ident:
        if (peek(1).kind == Tok::Assign) return parseAssign();
        if (isCompound(peek(1).kind)) return parseCompoundVar();
        if (typeDeclAhead()) return parseDecl(false);
        if (peek(1).kind == Tok::Ident && peek(2).kind == Tok::Assign)
          fail("Unknown type '" + t.text + "'.", t, "Declare it with: struct " + t.text + " { ... }");
        break;
      default: break;
    }
    auto s = mk(StmtKind::ExprStmt);
    Token start = cur();
    ExprP e = parseExpr();
    if (is(Tok::Assign)) {
      if (e->kind != ExprKind::Index && e->kind != ExprKind::Field)
        fail("Cannot assign to this expression.", cur(),
             "Only variables and array elements can be assigned.");
      next();
      s->kind = StmtKind::IndexAssign;
      s->target = std::move(e);
      s->expr = parseExpr();
    } else if (isCompound(cur().kind)) {
      if (e->kind != ExprKind::Index && e->kind != ExprKind::Field)
        fail("Cannot assign to this expression.", cur(),
             "Only variables and array elements can be assigned.");
      if (hasCall(*e))
        fail("A compound assignment cannot contain a function call in its target.", cur(),
             "Store the index in a variable first.");
      Token op = next();
      ExprP rhs = parseExpr();
      s->kind = StmtKind::IndexAssign;
      s->expr = compoundValue(op, cloneExpr(*e), std::move(rhs));
      s->target = std::move(e);
    } else {
      s->expr = std::move(e);
    }
    s->span = spanFrom(start);
    return s;
  }

  StmtP parseDecl(bool isConst) {
    auto s = mk(StmtKind::VarDecl);
    Token start = cur();  // 'const' or the type keyword
    s->isConst = isConst;
    if (isConst) {
      next();
      if (typeDeclAhead()) {
        s->hasType = true;
        s->declType = parseType();
      }
    } else {
      s->hasType = true;
      s->declType = parseType();
    }
    Token nm = expect(Tok::Ident, "a variable name");
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    if (!is(Tok::Assign)) {
      std::string ex = isConst ? "const " + nm.text + " = value"
                               : typeName(s->declType) + " " + nm.text + " = value";
      fail("Variable '" + nm.text + "' needs an initial value.", cur(), "Write: " + ex);
    }
    next();
    s->expr = parseExpr();
    s->span = spanFrom(start);
    return s;
  }

  StmtP parseAssign() {
    auto s = mk(StmtKind::Assign);
    Token nm = next();
    next();  // '='
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    s->expr = parseExpr();
    s->span = spanFrom(nm);
    return s;
  }

  StmtP parseIf() {
    Token kw = next();
    auto s = mk(StmtKind::If);
    s->span = {kw.line, kw.col, kw.endCol};
    s->expr = parseExpr();
    s->body = parseBlock();
    if (is(Tok::KwElse)) {
      next();
      s->hasElse = true;
      if (is(Tok::KwIf)) s->elseBody.push_back(parseIf());
      else s->elseBody = parseBlock();
    }
    return s;
  }

  StmtP parseFor() {
    Token kw = next();
    auto s = mk(StmtKind::For);
    s->span = {kw.line, kw.col, kw.endCol};
    Token nm = expect(Tok::Ident, "a loop variable name");
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    expect(Tok::KwIn, "'in'");
    s->expr = parseExpr();
    s->body = parseBlock();
    return s;
  }

  StmtP parseMatch() {
    Token kw = next();
    auto s = mk(StmtKind::Match);
    s->span = {kw.line, kw.col, kw.endCol};
    s->expr = parseExpr();
    Token open = expect(Tok::LBrace, "'{'");
    skipNl();
    while (!is(Tok::RBrace)) {
      if (is(Tok::End)) fail("Missing '}' to close the match opened here.", open);
      if (is(Tok::KwElse)) {
        next();
        s->hasElse = true;
        s->elseBody = parseBlock();
        skipNl();
        if (!is(Tok::RBrace)) fail("The 'else' arm must be the last arm of a 'match'.", cur());
      } else {
        Arm arm;
        do {
          arm.values.push_back(parseExpr());
        } while (match(Tok::Comma));
        arm.body = parseBlock();
        s->arms.push_back(std::move(arm));
        skipNl();
      }
    }
    next();
    return s;
  }

  StmtP parseDo() {
    Token kw = next();
    auto s = mk(StmtKind::DoWhile);
    s->span = {kw.line, kw.col, kw.endCol};
    s->body = parseBlock();
    if (!is(Tok::KwWhile))
      fail("Expected 'while' after the 'do' block but found " + describe(cur()) + ".", cur(),
           "Write: } while condition");
    next();
    s->expr = parseExpr();
    return s;
  }

  StmtP parseStruct() {
    Token kw = next();
    auto s = mk(StmtKind::StructDecl);
    s->span = {kw.line, kw.col, kw.endCol};
    Token nm = expect(Tok::Ident, "a struct name");
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    s->declType = structs.at(nm.text);
    Token open = expect(Tok::LBrace, "'{'");
    skipNl();
    while (!is(Tok::RBrace)) {
      if (is(Tok::End)) fail("Missing '}' to close the struct opened here.", open);
      if (!typeDeclAhead())
        fail("Expected a field like 'int x' but found " + describe(cur()) + ".", cur());
      Type ty = parseType();
      Token fn = expect(Tok::Ident, "a field name");
      s->params.push_back({ty, fn.text, {fn.line, fn.col, fn.endCol}});
      if (!is(Tok::Newline) && !is(Tok::RBrace)) fail("Put each field on its own line.", cur());
      skipNl();
    }
    next();
    return s;
  }

  StmtP parseWhile() {
    Token kw = next();
    auto s = mk(StmtKind::While);
    s->span = {kw.line, kw.col, kw.endCol};
    s->expr = parseExpr();
    s->body = parseBlock();
    return s;
  }

  StmtP parseReturn() {
    Token kw = next();
    auto s = mk(StmtKind::Return);
    if (!is(Tok::Newline) && !is(Tok::RBrace) && !is(Tok::End)) s->expr = parseExpr();
    s->span = spanFrom(kw);
    return s;
  }

  StmtP parseFn() {
    Token kw = next();
    auto s = mk(StmtKind::Fn);
    s->span = {kw.line, kw.col, kw.endCol};
    Token nm = expect(Tok::Ident, "a function name");
    s->name = nm.text;
    s->nameSpan = {nm.line, nm.col, nm.endCol};
    expect(Tok::LParen, "'('");
    if (!is(Tok::RParen)) {
      do {
        if (!typeDeclAhead())
          fail("Expected a parameter type but found " + describe(cur()) + ".", cur(),
               "Parameters are written as: type name, for example int a");
        Type ty = parseType();
        Token pn = expect(Tok::Ident, "a parameter name");
        s->params.push_back({ty, pn.text, {pn.line, pn.col, pn.endCol}});
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "')'");
    if (match(Tok::Arrow)) {
      size_t rk = 0;
      if (!scanType(rk))
        fail("Expected a return type after '->' but found " + describe(cur()) + ".", cur());
      s->retType = parseType();
    }
    s->body = parseBlock();
    return s;
  }

  std::vector<StmtP> parseBlock() {
    Token open = expect(Tok::LBrace, "'{'");
    std::vector<StmtP> v;
    skipNl();
    while (!is(Tok::RBrace)) {
      if (is(Tok::End)) fail("Missing '}' to close the block opened here.", open);
      v.push_back(parseStatement());
      skipNl();
    }
    next();
    return v;
  }

  // Expressions, lowest to highest precedence:
  //   ||   &&   == !=   < <= > >=   + -   * / %   unary - !
  ExprP parseExpr() { return parseTernary(); }

  // cond ? a : b   (lowest precedence, right-associative)
  ExprP parseTernary() {
    ExprP c = parseBinary(0);
    if (!is(Tok::Question)) return c;
    next();
    ExprP a = parseTernary();
    expect(Tok::Colon, "':'");
    ExprP b = parseTernary();
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Ternary;
    e->span = joinSpan(c->span, b->span);
    e->args.push_back(std::move(c));
    e->args.push_back(std::move(a));
    e->args.push_back(std::move(b));
    return e;
  }

  ExprP parseBinary(int level) {
    static const std::vector<std::vector<Tok>> levels = {
        {Tok::OrOr},
        {Tok::AndAnd},
        {Tok::EqEq, Tok::NotEq},
        {Tok::Lt, Tok::Le, Tok::Gt, Tok::Ge},
        {Tok::Pipe},
        {Tok::Caret},
        {Tok::Amp},
        {Tok::Shl, Tok::Shr},
        {Tok::Plus, Tok::Minus},
        {Tok::Star, Tok::Slash, Tok::Percent}};
    if (level == (int)levels.size()) return parseUnary();
    ExprP left = parseBinary(level + 1);
    while (std::find(levels[level].begin(), levels[level].end(), cur().kind) !=
           levels[level].end()) {
      Token op = next();
      ExprP right = parseBinary(level + 1);
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::Binary;
      e->op = op.kind;
      e->s = op.text;
      e->span = joinSpan(left->span, right->span);
      e->args.push_back(std::move(left));
      e->args.push_back(std::move(right));
      left = std::move(e);
    }
    return left;
  }

  ExprP parseUnary() {
    if (is(Tok::Minus) || is(Tok::Bang) || is(Tok::Tilde)) {
      Token op = next();
      ExprP operand = parseUnary();
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::Unary;
      e->op = op.kind;
      e->s = op.text;
      e->span = joinSpan({op.line, op.col, op.endCol}, operand->span);
      e->args.push_back(std::move(operand));
      return e;
    }
    return parsePostfix();
  }

  ExprP parsePostfix() {
    ExprP e = parsePrimary();
    while (is(Tok::LBracket) || is(Tok::Dot)) {
      if (is(Tok::Dot)) {
        next();
        Token fld = expect(Tok::Ident, "a field name");
        auto f = std::make_unique<Expr>();
        f->kind = ExprKind::Field;
        f->s = fld.text;
        f->span = joinSpan(e->span, {fld.line, fld.col, fld.endCol});
        f->args.push_back(std::move(e));
        e = std::move(f);
        continue;
      }
      next();
      ExprP idx = parseExpr();
      Token rb = expect(Tok::RBracket, "']'");
      auto n = std::make_unique<Expr>();
      n->kind = ExprKind::Index;
      n->span = joinSpan(e->span, {rb.line, rb.col, rb.endCol});
      n->args.push_back(std::move(e));
      n->args.push_back(std::move(idx));
      e = std::move(n);
    }
    return e;
  }

  ExprP lit(ExprKind k, const Token& t) {
    auto e = std::make_unique<Expr>();
    e->kind = k;
    e->span = {t.line, t.col, t.endCol};
    return e;
  }

  ExprP parsePrimary() {
    const Token t = cur();
    switch (t.kind) {
      case Tok::Int: { next(); auto e = lit(ExprKind::Int, t); e->i = t.intVal; return e; }
      case Tok::Float: { next(); auto e = lit(ExprKind::Float, t); e->f = t.floatVal; return e; }
      case Tok::String: { next(); auto e = lit(ExprKind::String, t); e->s = t.text; return e; }
      case Tok::Char: { next(); auto e = lit(ExprKind::Char, t); e->i = t.intVal; return e; }
      case Tok::KwTrue: { next(); auto e = lit(ExprKind::Bool, t); e->i = 1; return e; }
      case Tok::KwFalse: { next(); auto e = lit(ExprKind::Bool, t); e->i = 0; return e; }
      case Tok::Ident: {
        next();
        if (is(Tok::LParen)) return parseCall(t);
        auto e = lit(ExprKind::Var, t);
        e->s = t.text;
        return e;
      }
      case Tok::TInt: case Tok::TFloat: case Tok::TString: case Tok::TBool: case Tok::TChar:
        if (peek(1).kind == Tok::LParen) {
          next();
          return parseCall(t);
        }
        fail("Expected an expression but found type name " + describe(t) + ".", t,
             "Type names declare variables or convert values, for example int(x).");
      case Tok::LBrace: {
        next();
        auto e = lit(ExprKind::SetLit, t);
        if (!is(Tok::RBrace)) {
          ExprP first = parseExpr();
          if (is(Tok::Colon)) {
            e->kind = ExprKind::MapLit;
            next();
            e->args.push_back(std::move(first));
            e->args.push_back(parseExpr());
            while (match(Tok::Comma)) {
              e->args.push_back(parseExpr());
              expect(Tok::Colon, "':'");
              e->args.push_back(parseExpr());
            }
          } else {
            e->args.push_back(std::move(first));
            while (match(Tok::Comma)) e->args.push_back(parseExpr());
          }
        }
        Token rb = expect(Tok::RBrace, "'}'");
        e->span = {t.line, t.col, rb.line == t.line ? rb.endCol : t.endCol};
        return e;
      }
      case Tok::LBracket: {
        next();
        auto e = lit(ExprKind::Array, t);
        if (!is(Tok::RBracket)) {
          do {
            e->args.push_back(parseExpr());
          } while (match(Tok::Comma));
        }
        Token rb = expect(Tok::RBracket, "']'");
        e->span = {t.line, t.col, rb.line == t.line ? rb.endCol : t.endCol};
        return e;
      }
      case Tok::LParen: {
        next();
        ExprP e = parseExpr();
        Token rp = expect(Tok::RParen, "')'");
        e->span = {t.line, t.col, rp.line == t.line ? rp.endCol : e->span.endCol};
        return e;
      }
      default:
        fail("Expected an expression but found " + describe(t) + ".", t);
    }
  }

  ExprP parseCall(const Token& name) {
    next();  // '('
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::Call;
    e->s = name.text;
    if (!is(Tok::RParen)) {
      do {
        if (is(Tok::Ident) && peek(1).kind == Tok::Assign) {
          e->argNames.push_back(next().text);
          next();
        } else {
          e->argNames.push_back("");
        }
        e->args.push_back(parseExpr());
      } while (match(Tok::Comma));
    }
    Token rp = expect(Tok::RParen, "')'");
    e->span = {name.line, name.col, rp.line == name.line ? rp.endCol : name.endCol};
    return e;
  }
};

}  // namespace

Program parse(const std::vector<Token>& tokens) {
  Parser p(tokens);
  return p.parseProgram();
}

}  // namespace ipx
