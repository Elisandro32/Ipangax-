#include "lexer.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <unordered_map>

namespace ipx {
namespace {

[[noreturn]] void fail(const std::string& msg, int line, int col, int endCol,
                       const std::string& hint = "") {
  Diagnostic d;
  d.message = msg;
  d.hint = hint;
  d.line = line;
  d.col = col;
  d.endCol = endCol;
  throw DiagnosticError(d);
}

}  // namespace

std::vector<Token> lex(const std::string& s) {
  static const std::unordered_map<std::string, Tok> keywords = {
      {"fn", Tok::KwFn},       {"if", Tok::KwIf},         {"else", Tok::KwElse},
      {"while", Tok::KwWhile}, {"return", Tok::KwReturn}, {"const", Tok::KwConst},
      {"true", Tok::KwTrue},   {"false", Tok::KwFalse},   {"int", Tok::TInt},
      {"float", Tok::TFloat},  {"string", Tok::TString},  {"bool", Tok::TBool},
      {"char", Tok::TChar},
      {"for", Tok::KwFor},     {"in", Tok::KwIn},         {"break", Tok::KwBreak},
      {"continue", Tok::KwContinue}, {"match", Tok::KwMatch}, {"do", Tok::KwDo},
      {"struct", Tok::KwStruct}};

  std::vector<Token> out;
  int line = 1, col = 1, depth = 0;
  size_t i = 0;

  auto at = [&](size_t k) -> char { return k < s.size() ? s[k] : '\0'; };
  auto adv = [&]() {
    if (s[i] == '\n') { ++line; col = 1; } else { ++col; }
    ++i;
  };
  auto op = [&](Tok k, size_t len) {
    Token t;
    t.kind = k;
    t.text = s.substr(i, len);
    t.line = line;
    t.col = col;
    t.endCol = col + (int)len;
    for (size_t n = 0; n < len; ++n) adv();
    out.push_back(t);
  };
  auto escape = [&](int sl, int sc) -> char {
    // positioned on the character after the backslash
    if (i >= s.size()) fail("Unterminated escape sequence.", sl, sc, sc + 1);
    char e = s[i], r = 0;
    switch (e) {
      case 'n': r = '\n'; break;
      case 't': r = '\t'; break;
      case '0': r = '\0'; break;
      case '\\': r = '\\'; break;
      case '"': r = '"'; break;
      case '\'': r = '\''; break;
      default:
        fail(std::string("Unknown escape sequence '\\") + e + "'.", line, col - 1, col + 1,
             "Supported escapes: \\n \\t \\0 \\\\ \\\" \\'");
    }
    adv();
    return r;
  };

  while (i < s.size()) {
    char c = s[i];
    if (c == ' ' || c == '\t' || c == '\r') { adv(); continue; }
    if (c == '\n') {
      if (depth == 0 && !out.empty() && out.back().kind != Tok::Newline) {
        Token t;
        t.kind = Tok::Newline;
        t.text = "\\n";
        t.line = line;
        t.col = col;
        t.endCol = col + 1;
        out.push_back(t);
      }
      adv();
      continue;
    }
    if (c == '/' && at(i + 1) == '/') {
      while (i < s.size() && s[i] != '\n') adv();
      continue;
    }
    if (std::isdigit((unsigned char)c)) {
      int l = line, cl = col;
      size_t st = i;
      bool isFloat = false;
      while (std::isdigit((unsigned char)at(i))) adv();
      if (at(i) == '.' && std::isdigit((unsigned char)at(i + 1))) {
        isFloat = true;
        adv();
        while (std::isdigit((unsigned char)at(i))) adv();
      }
      Token t;
      t.text = s.substr(st, i - st);
      t.line = l;
      t.col = cl;
      t.endCol = col;
      if (isFloat) {
        t.kind = Tok::Float;
        t.floatVal = std::strtod(t.text.c_str(), nullptr);
      } else {
        errno = 0;
        long long v = std::strtoll(t.text.c_str(), nullptr, 10);
        if (errno == ERANGE)
          fail("Integer literal '" + t.text + "' is too large for int (64-bit).", l, cl, col);
        t.kind = Tok::Int;
        t.intVal = v;
      }
      out.push_back(t);
      continue;
    }
    if (std::isalpha((unsigned char)c) || c == '_') {
      int cl = col;
      size_t st = i;
      while (std::isalnum((unsigned char)at(i)) || at(i) == '_') adv();
      Token t;
      t.text = s.substr(st, i - st);
      t.line = line;
      t.col = cl;
      t.endCol = col;
      auto it = keywords.find(t.text);
      t.kind = it == keywords.end() ? Tok::Ident : it->second;
      out.push_back(t);
      continue;
    }
    if (c == '"') {
      int l = line, cl = col;
      adv();
      std::string v;
      while (true) {
        if (i >= s.size() || s[i] == '\n')
          fail("Unterminated string literal.", l, cl, cl + 1,
               "Close the string with '\"' on the same line.");
        char d = s[i];
        if (d == '"') { adv(); break; }
        if (d == '\\') { adv(); v += escape(l, cl); continue; }
        v += d;
        adv();
      }
      Token t;
      t.kind = Tok::String;
      t.text = v;
      t.line = l;
      t.col = cl;
      t.endCol = col;
      out.push_back(t);
      continue;
    }
    if (c == '\'') {
      int l = line, cl = col;
      adv();
      char ch = 0;
      if (at(i) == '\\') { adv(); ch = escape(l, cl); }
      else if (i >= s.size() || s[i] == '\'' || s[i] == '\n')
        fail("Empty or unterminated character literal.", l, cl, cl + 1);
      else { ch = s[i]; adv(); }
      if (at(i) != '\'')
        fail("A character literal must contain exactly one character.", l, cl, col,
             "Use double quotes for strings.");
      adv();
      Token t;
      t.kind = Tok::Char;
      t.text = std::string(1, ch);
      t.intVal = (unsigned char)ch;
      t.line = l;
      t.col = cl;
      t.endCol = col;
      out.push_back(t);
      continue;
    }
    switch (c) {
      case '+': if (at(i + 1) == '=') op(Tok::PlusEq, 2); else op(Tok::Plus, 1); break;
      case '*': if (at(i + 1) == '=') op(Tok::StarEq, 2); else op(Tok::Star, 1); break;
      case '/': if (at(i + 1) == '=') op(Tok::SlashEq, 2); else op(Tok::Slash, 1); break;
      case '%': if (at(i + 1) == '=') op(Tok::PercentEq, 2); else op(Tok::Percent, 1); break;
      case ',': op(Tok::Comma, 1); break;
      case '?': op(Tok::Question, 1); break;
      case ':': op(Tok::Colon, 1); break;
      case '[': ++depth; op(Tok::LBracket, 1); break;
      case ']': if (depth > 0) --depth; op(Tok::RBracket, 1); break;
      case '{': op(Tok::LBrace, 1); break;
      case '}': op(Tok::RBrace, 1); break;
      case '(': ++depth; op(Tok::LParen, 1); break;
      case ')': if (depth > 0) --depth; op(Tok::RParen, 1); break;
      case '-':
        if (at(i + 1) == '>') op(Tok::Arrow, 2);
        else if (at(i + 1) == '=') op(Tok::MinusEq, 2);
        else op(Tok::Minus, 1);
        break;
      case '=': if (at(i + 1) == '=') op(Tok::EqEq, 2); else op(Tok::Assign, 1); break;
      case '!': if (at(i + 1) == '=') op(Tok::NotEq, 2); else op(Tok::Bang, 1); break;
      case '<':
        if (at(i + 1) == '<' && at(i + 2) == '=') op(Tok::ShlEq, 3);
        else if (at(i + 1) == '<') op(Tok::Shl, 2);
        else if (at(i + 1) == '=') op(Tok::Le, 2);
        else op(Tok::Lt, 1);
        break;
      case '>':
        if (at(i + 1) == '>' && at(i + 2) == '=') op(Tok::ShrEq, 3);
        else if (at(i + 1) == '>') op(Tok::Shr, 2);
        else if (at(i + 1) == '=') op(Tok::Ge, 2);
        else op(Tok::Gt, 1);
        break;
      case '&':
        if (at(i + 1) == '&') op(Tok::AndAnd, 2);
        else if (at(i + 1) == '=') op(Tok::AmpEq, 2);
        else op(Tok::Amp, 1);
        break;
      case '|':
        if (at(i + 1) == '|') op(Tok::OrOr, 2);
        else if (at(i + 1) == '=') op(Tok::PipeEq, 2);
        else op(Tok::Pipe, 1);
        break;
      case '^': if (at(i + 1) == '=') op(Tok::CaretEq, 2); else op(Tok::Caret, 1); break;
      case '~': op(Tok::Tilde, 1); break;
      case '.': op(Tok::Dot, 1); break;
      default:
        fail(std::string("Unexpected character '") + c + "'.", line, col, col + 1);
    }
  }
  if (!out.empty() && out.back().kind != Tok::Newline) {
    Token t;
    t.kind = Tok::Newline;
    t.text = "\\n";
    t.line = line;
    t.col = col;
    t.endCol = col + 1;
    out.push_back(t);
  }
  Token e;
  e.kind = Tok::End;
  e.line = line;
  e.col = col;
  e.endCol = col + 1;
  out.push_back(e);
  return out;
}

std::string describe(const Token& t) {
  switch (t.kind) {
    case Tok::End: return "end of file";
    case Tok::Newline: return "end of line";
    case Tok::String: return "a string literal";
    case Tok::Char: return "a character literal";
    case Tok::Int:
    case Tok::Float: return "number '" + t.text + "'";
    default: return "'" + t.text + "'";
  }
}

}  // namespace ipx
