#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../diagnostics.hpp"

namespace ipx {

enum class Tok {
  Ident, Int, Float, String, Char,
  KwFn, KwIf, KwElse, KwWhile, KwReturn, KwConst, KwTrue, KwFalse,
  TInt, TFloat, TString, TBool, TChar,
  Plus, Minus, Star, Slash, Percent,
  Assign, EqEq, NotEq, Lt, Le, Gt, Ge, AndAnd, OrOr, Bang, Arrow,
  LParen, RParen, LBrace, RBrace, Comma,
  Newline, End,
  KwFor, KwIn, KwBreak, KwContinue, LBracket, RBracket,
  PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
  KwMatch, KwDo, Question, Colon,
  Amp, Pipe, Caret, Tilde, Shl, Shr, AmpEq, PipeEq, CaretEq, ShlEq, ShrEq, Dot, KwStruct
};

struct Token {
  Tok kind = Tok::End;
  std::string text;  // lexeme; decoded value for String and Char
  int64_t intVal = 0;
  double floatVal = 0;
  int line = 1, col = 1, endCol = 1;
};

// Throws DiagnosticError on the first lexical error.
std::vector<Token> lex(const std::string& source);
std::string describe(const Token& t);

}  // namespace ipx
