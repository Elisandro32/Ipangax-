#include <iostream>
#include <sstream>

#include "../compiler/ast/ast.hpp"
#include "../compiler/ipangax.hpp"
#include "../compiler/lexer/lexer.hpp"
#include "../compiler/parser/parser.hpp"

static int total = 0, failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++total;                                                                 \
    if (!(cond)) {                                                           \
      ++failures;                                                            \
      std::cerr << "FAIL line " << __LINE__ << ": " << #cond << "\n";        \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    ++total;                                                                 \
    std::string _a = (a), _b = (b);                                          \
    if (_a != _b) {                                                          \
      ++failures;                                                            \
      std::cerr << "FAIL line " << __LINE__ << ":\n  got:      " << _a        \
                << "\n  expected: " << _b << "\n";                           \
    }                                                                        \
  } while (0)

using namespace ipx;

static std::string kinds(const std::string& src) {
  static const char* names[] = {
      "Ident", "Int", "Float", "String", "Char", "fn", "if", "else", "while", "return", "const",
      "true", "false", "tint", "tfloat", "tstring", "tbool", "tchar", "+", "-", "*", "/", "%",
      "=", "==", "!=", "<", "<=", ">", ">=", "&&", "||", "!", "->", "(", ")", "{", "}", ",",
      "NL", "End", "for", "in", "break", "continue", "[", "]", "+=", "-=", "*=", "/=", "%=", "match", "do", "?", ":", "&", "|", "^", "~", "<<", ">>", "&=", "|=", "^=", "<<=", ">>=", ".", "struct"};
  std::string r;
  for (auto& t : lex(src)) r += std::string(r.empty() ? "" : " ") + names[(int)t.kind];
  return r;
}

static std::string ast(const std::string& src) {
  try {
    return dump(parse(lex(src)));
  } catch (const DiagnosticError& e) {
    return "ERROR: " + e.d.message;
  }
}

// First diagnostic message, or "" when the program is valid.
static std::string err(const std::string& src) {
  auto d = check(src);
  return d.empty() ? "" : d[0].message;
}

// Program output; on failure the output so far followed by "ERROR: message".
static std::string out(const std::string& src, RunOptions o = {}, const char* input = nullptr) {
  std::ostringstream os;
  std::istringstream is(input ? input : "");
  if (input) o.in = &is;
  std::vector<Diagnostic> d;
  bool ok = run(src, os, d, o);
  return ok ? os.str() : os.str() + "ERROR: " + d[0].message;
}

static void testLexer() {
  CHECK_EQ(kinds("x = 10 + 20"), "Ident = Int + Int NL End");
  CHECK_EQ(kinds("fn add(int a) -> int {}"),
           "fn Ident ( tint Ident ) -> tint { } NL End");
  CHECK_EQ(kinds("a <= b != c && d || !e"), "Ident <= Ident != Ident && Ident || ! Ident NL End");
  CHECK_EQ(kinds("x = 1 // comment\ny = 2"), "Ident = Int NL Ident = Int NL End");
  CHECK_EQ(kinds("f(1,\n  2)"), "Ident ( Int , Int ) NL End");  // no newline inside parens
  CHECK_EQ(kinds("x = 3.14 'a' \"s\""), "Ident = Float Char String NL End");
  CHECK_EQ(lex("\"a\\nb\"")[0].text, "a\nb");
  CHECK_EQ(lex("x = 1\n  y")[4].text, "y");
  CHECK(lex("x = 1")[0].line == 1 && lex("x = 1")[2].col == 5);
  try { lex("\"abc"); CHECK(false); } catch (const DiagnosticError& e) {
    CHECK_EQ(e.d.message, "Unterminated string literal.");
  }
  try { lex("x = @"); CHECK(false); } catch (const DiagnosticError& e) {
    CHECK_EQ(e.d.message, "Unexpected character '@'.");
  }
  try { lex("x = 99999999999999999999"); CHECK(false); } catch (const DiagnosticError& e) {
    CHECK(e.d.message.find("too large") != std::string::npos);
  }
}

static void testParser() {
  CHECK_EQ(ast("x = 1 + 2 * 3"), "(assign x (+ 1 (* 2 3)))");
  CHECK_EQ(ast("x = (1 + 2) * 3"), "(assign x (* (+ 1 2) 3))");
  CHECK_EQ(ast("x = 1 - 2 - 3"), "(assign x (- (- 1 2) 3))");
  CHECK_EQ(ast("x = -a * b"), "(assign x (* (- a) b))");
  CHECK_EQ(ast("x = a < b && c == d || !e"),
           "(assign x (|| (&& (< a b) (== c d)) (! e)))");
  CHECK_EQ(ast("int age = 20"), "(decl int age 20)");
  CHECK_EQ(ast("const PI = 3.5"), "(const PI 3.5)");
  CHECK_EQ(ast("fn add(int a, int b) -> int {\n  return a + b\n}"),
           "(fn add ((int a) (int b)) int (return (+ a b)))");
  CHECK_EQ(ast("fn greet(string n) {\n print(\"Hi \" + n)\n}"),
           "(fn greet ((string n)) void (expr (call print (+ \"Hi \" n))))");
  CHECK_EQ(ast("if a {\n x = 1\n} else if b {\n x = 2\n} else {\n x = 3\n}"),
           "(if a (then (assign x 1)) (else (if b (then (assign x 2)) (else (assign x 3)))))");
  CHECK_EQ(ast("while i < 3 {\n i = i + 1\n}"), "(while (< i 3) (assign i (+ i 1)))");
  CHECK_EQ(ast("x = int(\"5\")"), "(assign x (call int \"5\"))");

  CHECK(ast("x =").find("Expected an expression") != std::string::npos);
  CHECK(ast("if x {\n y = 1").find("Missing '}'") != std::string::npos);
  CHECK(ast("print(a) print(b)").find("after statement") != std::string::npos);
  CHECK(ast("if x {\n}\nelse {\n}").find("same line") != std::string::npos);
  CHECK(ast("int x").find("needs an initial value") != std::string::npos);
  CHECK(ast("fn f() {\n fn g() {}\n}").find("top level") != std::string::npos);
  CHECK(ast("fn f(a) {}").find("parameter type") != std::string::npos);
}

static void testChecker() {
  // valid programs
  CHECK_EQ(err("x = 1\ny = x + 2\nprint(y)"), "");
  CHECK_EQ(err("fn add(int a, int b) -> int {\n return a + b\n}\nprint(add(1, 2))"), "");
  CHECK_EQ(err("fn f(int n) -> int {\n if n > 0 {\n return 1\n } else {\n return 2\n }\n}"), "");
  CHECK_EQ(err("const K = 3\nfn f() -> int {\n return K\n}"), "");
  CHECK_EQ(err("x = 1\nif true {\n int x = 2\n print(x)\n}"), "");  // inner-scope shadowing

  // type errors
  CHECK_EQ(err("int x = \"hello\""),
           "Cannot assign a value of type 'string' to variable 'x' of type 'int'.");
  CHECK_EQ(err("name = \"a\"\nage = 20\nr = name + age"),
           "Cannot add values of type 'string' and 'int'.");
  CHECK_EQ(err("x = 1\nx = \"a\""), "Cannot assign a value of type 'string' to variable 'x' of type 'int'.");
  CHECK_EQ(err("x = 1 + 2.5"), "Cannot add values of type 'int' and 'float'.");
  CHECK_EQ(err("x = 5 % 2.0"), "Cannot take the remainder of values of type 'int' and 'float'.");
  CHECK_EQ(err("x = 1 < \"a\""), "Cannot compare values of type 'int' and 'string'.");
  CHECK_EQ(err("x = 1 && true"), "Operator '&&' requires 'bool' operands, got 'int' and 'bool'.");
  CHECK_EQ(err("x = -\"a\""), "Operator '-' cannot be applied to type 'string'.");
  CHECK_EQ(err("if 1 {\n}"), "The condition of 'if' must be 'bool', got 'int'.");
  CHECK_EQ(err("while \"a\" {\n}"), "The condition of 'while' must be 'bool', got 'string'.");

  // names and scope
  CHECK_EQ(err("print(score)"), "Undefined variable 'score'.");
  CHECK_EQ(check("score = 1\nprint(scor)")[0].hint, "Did you mean 'score'?");
  CHECK_EQ(err("x = foo(1)"), "Undefined function 'foo'.");
  CHECK_EQ(err("int x = 1\nint x = 2"), "Variable 'x' is already declared in this scope.");
  CHECK_EQ(err("if true {\n y = 1\n}\nprint(y)"), "Undefined variable 'y'.");
  CHECK_EQ(err("x = 1\nfn f() -> int {\n return x\n}"),
           "Variable 'x' is global and cannot be used inside a function in v0.1.");

  // constants
  CHECK_EQ(err("const A = 1\nA = 2"), "Cannot assign to constant 'A'.");
  CHECK(err("x = 1\nconst A = x").find("constant expression") != std::string::npos);

  // functions
  CHECK_EQ(err("fn f(int a) -> int {\n return a\n}\nx = f(1, 2)"),
           "Function 'f' expects 1 argument(s) but got 2.");
  CHECK_EQ(err("fn f(int a) -> int {\n return a\n}\nx = f(\"s\")"),
           "Argument 1 of 'f' must be 'int', got 'string'.");
  CHECK(err("fn f() -> int {\n print(1)\n}").find("may reach its end") != std::string::npos);
  CHECK_EQ(err("fn f() -> int {\n return \"a\"\n}"),
           "Cannot return a value of type 'string' from function 'f' declared to return 'int'.");
  CHECK(err("fn f() {\n return 1\n}").find("does not return a value") != std::string::npos);
  CHECK_EQ(err("fn g() {\n}\nx = g()"), "This expression has no value.");
  CHECK_EQ(err("return 1"), "'return' can only be used inside a function.");
  CHECK_EQ(err("fn f() {\n}\nfn f() {\n}"), "Function 'f' is already defined.");
  CHECK_EQ(err("fn print() {\n}"), "'print' is a built-in function and cannot be redefined.");
  CHECK_EQ(err("1 + 2"), "This expression has no effect.");

  // main rule
  CHECK_EQ(err("fn main() {\n print(1)\n}"), "");
  CHECK_EQ(err("fn main(int x) -> int {\n return x\n}"),
           "Function 'main' cannot take parameters in v0.1.");
  CHECK_EQ(err("fn main() -> int {\n return 1\n}"),
           "Function 'main' cannot return a value in v0.1.");

  // several errors reported at once, and no cascades from an erroneous operand
  CHECK(check("x = \"a\" + 1\ny = \"b\" + 2").size() == 2);
  CHECK(check("x = undefined1 + 1").size() == 1);

  // diagnostics carry positions
  auto d = check("a = 1\nb = a + \"s\"");
  CHECK(d.size() == 1 && d[0].line == 2 && d[0].col == 5 && d[0].endCol == 12);
}

static void testInterpreter() {
  CHECK_EQ(out("print(\"Hello, Ipangax\")"), "Hello, Ipangax\n");
  CHECK_EQ(out("x = 10\nprint(x)"), "10\n");
  CHECK_EQ(out("x = 10 + 20\nprint(x)"), "30\n");
  CHECK_EQ(out("name = \"Elisandro\"\nage = 20\n\nfn add(int a, int b) -> int {\n"
               "    return a + b\n}\n\nresult = add(10, 20)\n\nif result > 20 {\n"
               "    print(name)\n    print(result)\n}\n"),
           "Elisandro\n30\n");
  CHECK_EQ(out("fn main() {\n name = \"Ipangax\"\n print(name)\n}"), "Ipangax\n");
  CHECK_EQ(out("print(\"a\")\nfn main() {\n print(\"b\")\n}\nprint(\"c\")"), "a\nc\nb\n");

  // arithmetic
  CHECK_EQ(out("print(-7 / 2)\nprint(-7 % 3)\nprint(2 + 3 * 4)\nprint((2 + 3) * 4)"),
           "-3\n-1\n14\n20\n");
  CHECK_EQ(out("print(0.1 + 0.2)\nprint(1.5)\nprint(2.0)\nprint(7.0 / 2.0)"),
           "0.30000000000000004\n1.5\n2.0\n3.5\n");
  CHECK_EQ(out("print(\"ab\" + \"cd\")"), "abcd\n");
  CHECK_EQ(out("print(1 < 2)\nprint(\"a\" < \"b\")\nprint('a' == 'a')\nprint(true != false)"),
           "true\ntrue\ntrue\ntrue\n");

  // variables, scope, shadowing
  CHECK_EQ(out("x = 1\nx = x + 1\nprint(x)"), "2\n");
  CHECK_EQ(out("x = 1\nif true {\n x = 5\n}\nprint(x)"), "5\n");
  CHECK_EQ(out("x = 1\nif true {\n int x = 5\n print(x)\n}\nprint(x)"), "5\n1\n");
  CHECK_EQ(out("const PI = 3.5\nprint(PI)"), "3.5\n");
  CHECK_EQ(out("const K = 2\nfn f(int a) -> int {\n return a * K\n}\nprint(f(21))"), "42\n");

  // control flow
  CHECK_EQ(out("s = 85\nif s >= 90 {\n print(\"A\")\n} else if s >= 70 {\n print(\"B\")\n} else {\n print(\"C\")\n}"), "B\n");
  CHECK_EQ(out("i = 0\nsum = 0\nwhile i < 5 {\n i = i + 1\n sum = sum + i\n}\nprint(sum)"), "15\n");

  // functions
  CHECK_EQ(out("fn fact(int n) -> int {\n if n <= 1 {\n  return 1\n }\n return n * fact(n - 1)\n}\nprint(fact(10))"),
           "3628800\n");
  CHECK_EQ(out("fn fib(int n) -> int {\n if n < 2 {\n  return n\n }\n return fib(n - 1) + fib(n - 2)\n}\nprint(fib(15))"),
           "610\n");
  CHECK_EQ(out("fn greet(string n) {\n print(\"Hello \" + n)\n}\ngreet(\"Ipangax\")"), "Hello Ipangax\n");
  CHECK_EQ(out("fn f() {\n print(1)\n return\n}\nf()"), "1\n");
  CHECK_EQ(out("fn loop() -> int {\n i = 0\n while true {\n  i = i + 1\n  if i == 3 {\n   return i\n  }\n }\n return 0\n}\nprint(loop())"), "3\n");

  // conversions
  CHECK_EQ(out("print(int(3.9))\nprint(float(2))\nprint(string(42) + \"!\")\nprint(int(\"12\") + 1)\nprint(int('A'))"),
           "3\n2.0\n42!\n13\n65\n");

  // short-circuit
  CHECK_EQ(out("x = 0\nprint(x != 0 && 10 / x > 1)\nprint(x == 0 || 10 / x > 1)"), "false\ntrue\n");

  // runtime errors keep earlier output
  CHECK_EQ(out("print(1)\nx = 0\nprint(5 / x)"), "1\nERROR: Division by zero.");
  CHECK_EQ(out("print(5.0 / 0.0)"), "ERROR: Division by zero.");
  CHECK_EQ(out("x = 9223372036854775807\nprint(x + 1)"), "ERROR: Integer overflow in addition.");
  CHECK_EQ(out("print(int(\"abc\"))"), "ERROR: Cannot convert string \"abc\" to int.");
  CHECK_EQ(out("fn f(int n) -> int {\n return f(n + 1)\n}\nprint(f(0))"),
           "ERROR: Maximum call depth (1000) exceeded.");
  RunOptions lim;
  lim.maxSteps = 1000;
  CHECK_EQ(out("while true {\n}", lim), "ERROR: Execution step limit exceeded.");

  CHECK_EQ(out("s = \"ab\"\nwhile true {\n s = s + s\n}"), "ERROR: String too long (limit is 10 million characters).");

  // compile errors never run anything
  CHECK_EQ(out("print(1)\nint x = \"hello\""), "ERROR: Cannot assign a value of type 'string' to variable 'x' of type 'int'.");
}

static void testV02() {
  // lexer
  CHECK_EQ(kinds("a[0]"), "Ident [ Int ] NL End");
  CHECK_EQ(kinds("for x in a {}"), "for Ident in Ident { } NL End");
  CHECK_EQ(kinds("x = [1,\n 2]"), "Ident = [ Int , Int ] NL End");
  CHECK_EQ(kinds("break\ncontinue"), "break NL continue NL End");

  // parser
  CHECK_EQ(ast("int[] a = [1, 2, 3]"), "(decl int[] a (array 1 2 3))");
  CHECK_EQ(ast("int[][] g = [[1], [2]]"), "(decl int[][] g (array (array 1) (array 2)))");
  CHECK_EQ(ast("a[0] = 5"), "(set (index a 0) 5)");
  CHECK_EQ(ast("x = a[1][2] + 1"), "(assign x (+ (index (index a 1) 2) 1))");
  CHECK_EQ(ast("for i in range(3) {\n print(i)\n}"), "(for i (call range 3) (expr (call print i)))");
  CHECK_EQ(ast("while true {\n break\n continue\n}"), "(while true (break) (continue))");
  CHECK_EQ(ast("fn f(int[] a) -> int[] {\n return a\n}"), "(fn f ((int[] a)) int[] (return a))");
  CHECK_EQ(ast("x = int(\"1\")"), "(assign x (call int \"1\"))");
  CHECK(ast("x + 1 = 2").find("Cannot assign to this expression.") != std::string::npos);
  CHECK(ast("a = [1, 2").find("Expected ']'") != std::string::npos);

  // checker: valid
  CHECK_EQ(err("int[] a = [1, 2]\na[0] = 5\nprint(a[1])"), "");
  CHECK_EQ(err("int[] a = []\npush(a, 1)"), "");
  CHECK_EQ(err("g = [[1, 2], [3]]\nx = g[0][1] + 1"), "");
  CHECK_EQ(err("for i in range(3) {\n print(i)\n}"), "");
  CHECK_EQ(err("for c in \"abc\" {\n print(c)\n}"), "");
  CHECK_EQ(err("fn f(int[] a) -> int {\n return a[0]\n}\nprint(f([1]))"), "");
  CHECK_EQ(err("fn f(int[] a) -> int {\n return len(a)\n}\nprint(f([]))"), "");

  // checker: errors
  CHECK_EQ(err("int[] a = [\"x\"]"), "Cannot assign a value of type 'string[]' to variable 'a' of type 'int[]'.");
  CHECK_EQ(err("a = [1, \"x\"]"), "Array elements must have the same type: expected 'int', got 'string'.");
  CHECK_EQ(err("a = []"), "Cannot infer the type of an empty array.");
  CHECK_EQ(err("a = [1]\na[0] = \"x\""), "Cannot assign a value of type 'string' to an element of type 'int'.");
  CHECK_EQ(err("a = [1]\nx = a[\"i\"]"), "An index must be 'int', got 'string'.");
  CHECK_EQ(err("x = 5\ny = x[0]"), "Cannot index a value of type 'int'.");
  CHECK_EQ(err("s = \"a\"\ns[0] = 'b'"), "Strings are immutable; their characters cannot be assigned.");
  CHECK_EQ(err("break"), "'break' can only be used inside a loop.");
  CHECK_EQ(err("for x in 5 {\n}"), "Cannot loop over a value of type 'int'.");
  CHECK_EQ(err("for i in range(3) {\n}\nprint(i)"), "Undefined variable 'i'.");
  CHECK_EQ(err("x = range(3)"), "range() can only be used as the iterable of a 'for' loop.");
  CHECK_EQ(err("fn len() {\n}"), "'len' is a built-in function and cannot be redefined.");
  CHECK_EQ(err("const A = [1]"), "Constants cannot be arrays in v0.2.");
  CHECK_EQ(err("x = len(5)"), "len() needs a string or a collection, got 'int'.");
  CHECK_EQ(err("a = [1]\npush(a, \"s\")"), "Cannot push a value of type 'string' into an array of 'int'.");
  CHECK_EQ(err("x = min(1, 2.0)"), "'min' needs two int or two float values, got 'int' and 'float'.");
  CHECK_EQ(err("a = [1]\nb = [2]\nx = a == b"), "Cannot compare values of type 'int[]' and 'int[]'.");
  CHECK_EQ(err("for c in \"abc\" {\n x = c + 1\n}"), "Cannot add values of type 'char' and 'int'.");
  CHECK_EQ(err("x = sqrt(4)"), "sqrt() needs a float, got 'int'.");
  CHECK_EQ(err("x = int([1])"), "Cannot convert a value of type 'int[]' to 'int'.");

  // interpreter: arrays
  CHECK_EQ(out("a = [3, 1, 2]\nprint(a)\nprint(len(a))\na[0] = 10\nprint(a[0])\npush(a, 7)\nprint(a)"),
           "[3, 1, 2]\n3\n10\n[10, 1, 2, 7]\n");
  CHECK_EQ(out("a = [1, 2]\nprint(a[2])"), "ERROR: Index 2 is out of bounds for length 2.");
  CHECK_EQ(out("a = [1, 2]\nx = 0 - 1\nprint(a[x])"), "ERROR: Index -1 is out of bounds for length 2.");
  CHECK_EQ(out("a = [1]\na[3] = 0"), "ERROR: Index 3 is out of bounds for length 1.");
  CHECK_EQ(out("a = [1]\nb = a\nb[0] = 9\nprint(a[0])\nc = copy(a)\nc[0] = 1\nprint(a[0])"), "9\n9\n");
  CHECK_EQ(out("fn bump(int[] a) {\n a[0] = a[0] + 1\n}\nx = [1]\nbump(x)\nprint(x[0])"), "2\n");
  CHECK_EQ(out("g = make(2, make(2, 0))\ng[0][1] = 5\nprint(g)"), "[[0, 5], [0, 0]]\n");
  CHECK_EQ(out("a = [1, 2]\nx = pop(a)\nprint(x)\nprint(a)"), "2\n[1]\n");
  CHECK_EQ(out("int[] a = []\npop(a)"), "ERROR: pop() on an empty array.");
  CHECK_EQ(out("a = [3, 1, 2]\nsort(a)\nprint(a)\ns = [\"b\", \"a\"]\nsort(s)\nprint(s)"),
           "[1, 2, 3]\n[\"a\", \"b\"]\n");
  CHECK_EQ(out("print([\"a\", \"b\"])\nprint(['x'])\nprint([1.5, 2.0])\nprint([true])"),
           "[\"a\", \"b\"]\n['x']\n[1.5, 2.0]\n[true]\n");

  // interpreter: loops
  CHECK_EQ(out("s = 0\nfor i in range(5) {\n s = s + i\n}\nprint(s)"), "10\n");
  CHECK_EQ(out("for i in range(2, 5) {\n print(i)\n}"), "2\n3\n4\n");
  CHECK_EQ(out("for i in range(5, 0, -2) {\n print(i)\n}"), "5\n3\n1\n");
  CHECK_EQ(out("for x in [1, 2, 3] {\n print(x * 2)\n}"), "2\n4\n6\n");
  CHECK_EQ(out("for c in \"ab\" {\n print(c)\n}"), "a\nb\n");
  CHECK_EQ(out("for i in range(10) {\n if i == 3 {\n  break\n }\n if i % 2 == 0 {\n  continue\n }\n print(i)\n}"), "1\n");
  CHECK_EQ(out("i = 0\nwhile true {\n i = i + 1\n if i < 3 {\n  continue\n }\n break\n}\nprint(i)"), "3\n");
  CHECK_EQ(out("fn f() -> int {\n for i in range(10) {\n  if i == 4 {\n   return i\n  }\n }\n return 0\n}\nprint(f())"), "4\n");
  CHECK_EQ(out("for i in range(0, 3, 0) {\n}"), "ERROR: range() step cannot be zero.");
  CHECK_EQ(out("a = [1]\nfor x in a {\n if len(a) < 4 {\n  push(a, x)\n }\n}\nprint(len(a))"), "4\n");

  // interpreter: strings and math
  CHECK_EQ(out("s = \"hello\"\nprint(len(s))\nprint(s[1])"), "5\ne\n");
  CHECK_EQ(out("print(split(\"  a b  c \"))\nprint(join(split(\"a b c\"), \"-\"))"), "[\"a\", \"b\", \"c\"]\na-b-c\n");
  CHECK_EQ(out("print(min(3, 5))\nprint(max(2.5, 1.5))\nprint(abs(-4))\nprint(pow(2, 10))\nprint(sqrt(16.0))"),
           "3\n2.5\n4\n1024\n4.0\n");
  CHECK_EQ(out("print(pow(10, 30))"), "ERROR: Integer overflow in pow().");
  CHECK_EQ(out("print(pow(2, 62))\nprint(pow(0, 0))"), "4611686018427387904\n1\n");

  // interpreter: input
  CHECK_EQ(out("n = int(input())\ntotal = 0\nfor x in split(input()) {\n total = total + int(x)\n}\nprint(n)\nprint(total)", {}, "3\n1 2 3\n"), "3\n6\n");
  CHECK_EQ(out("print(input())", {}, "line one\r\nline two"), "line one\n");
  CHECK_EQ(out("a = input()\nb = input()\nprint(b)", {}, "x\ny"), "y\n");
  CHECK_EQ(out("a = input()", {}, ""), "ERROR: No more input to read.");
  CHECK_EQ(out("a = input()"), "ERROR: input() is not available here.");

  // limits
  RunOptions arr;
  arr.maxArray = 1000;
  CHECK_EQ(out("a = [1]\nwhile true {\n push(a, 1)\n}", arr), "ERROR: Collection too large (limit is 1000 elements).");
  CHECK_EQ(out("a = make(5000, 0)", arr), "ERROR: Collection too large (limit is 1000 elements).");
}

static void testCompound() {
  CHECK_EQ(kinds("x += 1\ny -= 2\nz *= 3\nw /= 4\nv %= 5"),
           "Ident += Int NL Ident -= Int NL Ident *= Int NL Ident /= Int NL Ident %= Int NL End");
  CHECK_EQ(kinds("a->b"), "Ident -> Ident NL End");
  CHECK_EQ(ast("x += 2"), "(assign x (+ x 2))");
  CHECK_EQ(ast("x %= 2 + 1"), "(assign x (% x (+ 2 1)))");
  CHECK_EQ(ast("a[i] -= 1"), "(set (index a i) (- (index a i) 1))");
  CHECK(ast("a[f()] += 1").find("function call") != std::string::npos);
  CHECK(ast("f() += 1").find("Cannot assign to this expression.") != std::string::npos);

  CHECK_EQ(err("x = 1\nx += 2"), "");
  CHECK_EQ(err("s = \"a\"\ns += 1"), "Cannot add values of type 'string' and 'int'.");
  CHECK_EQ(err("x = 1.5\nx -= 1"), "Cannot subtract values of type 'float' and 'int'.");
  CHECK_EQ(err("x += 1"), "Undefined variable 'x'.");
  CHECK_EQ(err("const K = 1\nK += 1"), "Cannot assign to constant 'K'.");

  CHECK_EQ(out("x = 10\nx += 5\nx -= 3\nx *= 2\nx /= 4\nx %= 4\nprint(x)"), "2\n");
  CHECK_EQ(out("s = \"a\"\ns += \"b\"\nprint(s)"), "ab\n");
  CHECK_EQ(out("a = [1, 2, 3]\na[1] += 10\na[0] *= 5\nprint(a)"), "[5, 12, 3]\n");
  CHECK_EQ(out("g = make(2, make(2, 1))\ng[1][0] += 4\nprint(g)"), "[[1, 1], [5, 1]]\n");
  CHECK_EQ(out("x = 9223372036854775807\nx += 1"), "ERROR: Integer overflow in addition.");
  CHECK_EQ(out("x = 0\nx /= 0"), "ERROR: Division by zero.");
  CHECK_EQ(out("t = 0\nfor i in range(5) {\n t += i\n}\nprint(t)"), "10\n");

  // slots: recursion, nested scopes, shadowing and globals
  CHECK_EQ(out("fn f(int n) -> int {\n a = n\n if n > 0 {\n  b = f(n - 1)\n  a = a + b\n }\n return a\n}\nprint(f(4))"), "10\n");
  CHECK_EQ(out("x = 1\nif true {\n int x = 2\n x += 1\n print(x)\n}\nprint(x)"), "3\n1\n");
  CHECK_EQ(out("const K = 3\nfn f() -> int {\n t = 0\n for i in range(K) {\n  t += i\n }\n return t\n}\nprint(f())"), "3\n");
  CHECK_EQ(out("fn f() -> int {\n return K\n}\nx = f()\nconst K = 5"), "ERROR: Undefined variable 'K'.");
  CHECK_EQ(out("x = f()\nconst K = 5\nfn f() -> int {\n return K\n}"), "ERROR: 'K' is used before it is initialized.");
}

static void testControl() {
  CHECK_EQ(kinds("a ? b : c"), "Ident ? Ident : Ident NL End");
  CHECK_EQ(kinds("match x {}\ndo {}"), "match Ident { } NL do { } NL End");
  CHECK_EQ(ast("x = a > 0 ? 1 : 2"), "(assign x (? (> a 0) 1 2))");
  CHECK_EQ(ast("x = a ? 1 : b ? 2 : 3"), "(assign x (? a 1 (? b 2 3)))");
  CHECK_EQ(ast("x = 1 + (a ? 2 : 3)"), "(assign x (+ 1 (? a 2 3)))");
  CHECK_EQ(ast("do {\n i += 1\n} while i < 3"), "(do (assign i (+ i 1)) while (< i 3))");
  CHECK_EQ(ast("match n {\n 1, 2 {\n  print(1)\n }\n 3 {\n  print(3)\n }\n else {\n  print(0)\n }\n}"),
           "(match n (arm (1 2) (expr (call print 1))) (arm (3) (expr (call print 3))) (else (expr (call print 0))))");
  CHECK(ast("x = a ? 1").find("Expected ':'") != std::string::npos);
  CHECK(ast("do {\n}\nprint(1)").find("Expected 'while'") != std::string::npos);
  CHECK(ast("match x {\n else {\n }\n 1 {\n }\n}").find("must be the last") != std::string::npos);
  CHECK(ast("match x {\n else {\n }\n else {\n }\n}").find("must be the last") != std::string::npos);

  CHECK_EQ(err("x = true ? 1 : 2"), "");
  CHECK_EQ(err("x = 1 ? 1 : 2"), "The condition of '?:' must be 'bool', got 'int'.");
  CHECK_EQ(err("x = true ? 1 : \"a\""), "Both branches of '?:' must have the same type, got 'int' and 'string'.");
  CHECK_EQ(err("x = true ? 1 : 2.0"), "Both branches of '?:' must have the same type, got 'int' and 'float'.");
  CHECK_EQ(err("n = 1\nmatch n {\n 1 {\n }\n}"), "");
  CHECK_EQ(err("n = 1\nmatch n {\n \"a\" {\n }\n}"), "This arm has type 'string' but the match value has type 'int'.");
  CHECK_EQ(err("n = 1.5\nmatch n {\n 1 {\n }\n}"), "'match' needs an int, string, char or bool, got 'float'.");
  CHECK_EQ(err("n = 1\nmatch n {\n else {\n }\n}"), "A 'match' needs at least one arm besides 'else'.");
  CHECK_EQ(err("i = 0\ndo {\n i += 1\n} while i"), "The condition of 'do-while' must be 'bool', got 'int'.");
  CHECK_EQ(err("do {\n break\n} while true"), "");
  CHECK_EQ(err("do {\n y = 1\n} while y > 0"), "Undefined variable 'y'.");
  CHECK_EQ(err("fn f(int n) -> int {\n match n {\n  1 {\n   return 10\n  }\n  else {\n   return 20\n  }\n }\n}"), "");
  CHECK(err("fn f(int n) -> int {\n match n {\n  1 {\n   return 10\n  }\n }\n}").find("may reach its end") != std::string::npos);

  CHECK_EQ(out("x = 5\nprint(x > 3 ? \"big\" : \"small\")"), "big\n");
  CHECK_EQ(out("a = 0\nprint(a == 0 ? 1 : 10 / a)"), "1\n");  // only the chosen branch runs
  CHECK_EQ(out("n = 5\nprint(n % 2 == 0 ? \"even\" : n > 3 ? \"big odd\" : \"small odd\")"), "big odd\n");
  CHECK_EQ(out("t = 0\ndo {\n t += 1\n} while t < 3\nprint(t)"), "3\n");
  CHECK_EQ(out("t = 10\ndo {\n t += 1\n} while t < 3\nprint(t)"), "11\n");  // body runs once
  CHECK_EQ(out("t = 0\ndo {\n t += 1\n if t == 2 {\n  break\n }\n} while true\nprint(t)"), "2\n");
  CHECK_EQ(out("t = 0\nn = 0\ndo {\n t += 1\n if t % 2 == 0 {\n  continue\n }\n n += 1\n} while t < 6\nprint(n)"), "3\n");
  CHECK_EQ(out("fn name(int d) -> string {\n match d {\n  1 {\n   return \"Mon\"\n  }\n  2, 3 {\n   return \"Tue or Wed\"\n  }\n  else {\n   return \"other\"\n  }\n }\n}\nprint(name(1))\nprint(name(3))\nprint(name(9))"),
           "Mon\nTue or Wed\nother\n");
  CHECK_EQ(out("s = \"b\"\nmatch s {\n \"a\" {\n  print(1)\n }\n \"b\" {\n  print(2)\n }\n}"), "2\n");
  CHECK_EQ(out("c = 'x'\nmatch c {\n 'x' {\n  print(\"ex\")\n }\n}"), "ex\n");
  CHECK_EQ(out("match 7 {\n 1 {\n  print(1)\n }\n}\nprint(\"after\")"), "after\n");  // no arm, no else: nothing happens
  CHECK_EQ(out("k = 0\nmatch 1 {\n 1 {\n  k = 5\n }\n}\nprint(k)"), "5\n");
  CHECK_EQ(out("for i in range(5) {\n match i {\n  2 {\n   continue\n  }\n  4 {\n   break\n  }\n  else {\n   print(i)\n  }\n }\n}"), "0\n1\n3\n");
  CHECK_EQ(out("x = 3\nmatch x {\n 1 + 2 {\n  print(\"three\")\n }\n}"), "three\n");  // arms may be expressions
}

static void testBigPackage() {
  // ---- lexer / parser
  CHECK_EQ(kinds("a & b | c ^ ~d"), "Ident & Ident | Ident ^ ~ Ident NL End");
  CHECK_EQ(kinds("a << 1 >> 2"), "Ident << Int >> Int NL End");
  CHECK_EQ(kinds("x <<= 1\nx >>= 1\nx &= 1\nx |= 1\nx ^= 1"), "Ident <<= Int NL Ident >>= Int NL Ident &= Int NL Ident |= Int NL Ident ^= Int NL End");
  CHECK_EQ(kinds("p.x"), "Ident . Ident NL End");
  CHECK_EQ(ast("x = a & b | c"), "(assign x (| (& a b) c))");
  CHECK_EQ(ast("x = a | b ^ c & d"), "(assign x (| a (^ b (& c d))))");
  CHECK_EQ(ast("x = 1 + 2 << 3"), "(assign x (<< (+ 1 2) 3))");
  CHECK_EQ(ast("x = a & 1 == 0"), "(assign x (== (& a 1) 0))");
  CHECK_EQ(ast("x = ~a"), "(assign x (~ a))");
  CHECK_EQ(ast("x ^= 3"), "(assign x (^ x 3))");
  CHECK_EQ(ast("a[i] <<= 1"), "(set (index a i) (<< (index a i) 1))");
  CHECK_EQ(ast("struct P {\n int x\n int y\n}"), "(struct P (int x) (int y))");
  CHECK_EQ(ast("struct P {\n int x\n}\np = P(x = 1)"), "(struct P (int x))\n(assign p (call P x=1))");
  CHECK_EQ(ast("struct P {\n int x\n}\np.x = 5"), "(struct P (int x))\n(set (field p x) 5)");
  CHECK_EQ(ast("v = p.x + 1"), "(assign v (+ (field p x) 1))");
  CHECK_EQ(ast("map[string, int] m = {}"), "(decl map[string, int] m (set))");
  CHECK_EQ(ast("m = {\"a\": 1, \"b\": 2}"), "(assign m (map \"a\" 1 \"b\" 2))");
  CHECK_EQ(ast("s = {1, 2}"), "(assign s (set 1 2))");
  CHECK_EQ(ast("heap[int] h = {}"), "(decl heap[int] h (set))");
  CHECK_EQ(ast("map[int, map[int, int]] g = {}"), "(decl map[int, map[int, int]] g (set))");
  CHECK_EQ(ast("deque[int][] a = []"), "(decl deque[int][] a (array))");
  CHECK(ast("Foo f = 1").find("Unknown type 'Foo'.") != std::string::npos);
  CHECK(ast("fn f() {\n struct P {\n }\n}").find("top level") != std::string::npos);

  // variables named like collection types still work
  CHECK_EQ(out("set = [5, 6]\nprint(set[1])"), "6\n");
  CHECK_EQ(out("map = [1, 2]\nk = 0\nprint(map[k])"), "1\n");

  // ---- bits
  CHECK_EQ(err("x = 1.5 & 2"), "Cannot apply a bitwise operator to values of type 'float' and 'int'.");
  CHECK_EQ(err("x = ~1.5"), "Operator '~' cannot be applied to type 'float'.");
  CHECK_EQ(out("print(6 & 3)\nprint(6 | 3)\nprint(6 ^ 3)\nprint(~5)\nprint(1 << 10)\nprint(1024 >> 3)\nprint(-8 >> 1)"),
           "2\n7\n5\n-6\n1024\n128\n-4\n");
  CHECK_EQ(out("x = 5\nx <<= 2\nx |= 1\nx &= 13\nx ^= 4\nprint(x)"), "1\n");
  CHECK_EQ(out("print(5 & 1 == 1)\nprint(1 + 2 << 3)"), "true\n24\n");
  CHECK_EQ(out("print(1 << 62)"), "4611686018427387904\n");
  CHECK_EQ(out("print(1 << 64)"), "ERROR: Shift count must be between 0 and 63.");
  CHECK_EQ(out("print(1 << 63)"), "ERROR: Integer overflow in shift.");
  CHECK_EQ(out("a = [1, 2]\na[1] <<= 3\nprint(a)"), "[1, 16]\n");

  // ---- math
  CHECK_EQ(err("x = gcd(1.5, 2)"), "Argument 1 of 'gcd' must be 'int', got 'float'.");
  CHECK_EQ(err("x = floor(2)"), "floor() needs a float, got 'int'.");
  CHECK_EQ(err("x = PI * 2.0"), "");
  CHECK_EQ(out("print(gcd(12, 18))\nprint(gcd(-12, 18))\nprint(lcm(4, 6))\nprint(lcm(0, 5))"), "6\n6\n12\n0\n");
  CHECK_EQ(out("print(floor(2.7))\nprint(floor(-2.1))\nprint(ceil(2.1))\nprint(round(2.5))\nprint(round(-2.5))"), "2\n-3\n3\n3\n-3\n");
  CHECK_EQ(out("print(powmod(2, 10, 1000))\nprint(powmod(7, 0, 5))\nprint(powmod(-2, 3, 5))"), "24\n1\n2\n");
  CHECK_EQ(out("print(floor(log2(1024.0)))\nprint(exp(0.0))\nprint(sin(0.0))\nprint(cos(0.0))"), "10\n1.0\n0.0\n1.0\n");
  CHECK_EQ(out("print(floor(atan2(1.0, 1.0) * 4.0 * 1000.0))"), "3141\n");
  CHECK_EQ(out("print(PI > 3.14 && PI < 3.15)\nprint(E > 2.7 && E < 2.8)"), "true\ntrue\n");
  CHECK_EQ(out("PI = 3.0\nprint(PI)"), "3.0\n");
  CHECK_EQ(out("print(log(0.0))"), "ERROR: log() of a non-positive number.");
  CHECK_EQ(out("print(floor(1000000.0 * 1000000.0 * 1000000.0 * 1000000.0))"), "ERROR: floor() result is out of the int range.");
  CHECK_EQ(out("print(powmod(2, 3, 0))"), "ERROR: powmod() needs a positive modulus.");
  // user functions may reuse the newer built-in names
  CHECK_EQ(out("fn add(int a, int b) -> int {\n return a + b\n}\nfn get(int a) -> int {\n return a * 2\n}\nprint(add(1, 2))\nprint(get(4))"), "3\n8\n");
  CHECK_EQ(err("fn print() {\n}"), "'print' is a built-in function and cannot be redefined.");

  // ---- maps
  CHECK_EQ(err("map[float, int] m = {}"), "Map keys and set elements must be int, string, char or bool, got 'float'.");
  CHECK_EQ(err("map[string, int] m = {}\nm[1] = 2"), "The key must be 'string', got 'int'.");
  CHECK_EQ(err("map[string, int] m = {}\nm[\"a\"] = \"x\""), "Cannot assign a value of type 'string' to a map value of type 'int'.");
  CHECK_EQ(err("m = {}"), "Cannot infer the type of '{}'.");
  CHECK_EQ(err("m = {\"a\": 1, \"b\": \"x\"}"), "Map values must have the same type: expected 'int', got 'string'.");
  CHECK_EQ(err("s = {1, \"a\"}"), "Set elements must have the same type: expected 'int', got 'string'.");
  CHECK_EQ(err("x = has(5, 1)"), "has() needs a map, set or array, got 'int'.");
  CHECK_EQ(err("x = keys(5)"), "keys() needs a map or a set, got 'int'.");
  CHECK_EQ(err("map[string, int] m = {}\nx = m[\"a\"] + 1"), "");
  CHECK_EQ(out(R"IPX(map[string, int] m = {}
m["a"] = 2
m["b"] = 5
m["a"] += 10
print(m)
print(len(m))
print(has(m, "a"))
print(has(m, "z"))
print(get(m, "z", -1))
print(keys(m))
print(values(m))
remove(m, "a")
print(m))IPX"),
           "{\"a\": 12, \"b\": 5}\n2\ntrue\nfalse\n-1\n[\"a\", \"b\"]\n[12, 5]\n{\"b\": 5}\n");
  CHECK_EQ(out("map[string, int] m = {}\nprint(m[\"x\"])"), "ERROR: Key not found: \"x\".");
  CHECK_EQ(out("m = {3: \"c\", 1: \"a\"}\nprint(m)\nprint(first(m))\nprint(last(m))"), "{1: \"a\", 3: \"c\"}\n1\n3\n");
  CHECK_EQ(out("m = {2: 20, 1: 10}\nfor k in m {\n print(k + m[k])\n}"), "11\n22\n");
  CHECK_EQ(out("map[int, int] cnt = {}\nfor x in [1, 2, 1, 1] {\n cnt[x] = get(cnt, x, 0) + 1\n}\nprint(cnt)"), "{1: 3, 2: 1}\n");
  CHECK_EQ(out("map[string, int[]] g = {}\ng[\"a\"] = []\npush(g[\"a\"], 5)\nprint(g)"), "{\"a\": [5]}\n");
  CHECK_EQ(out("map[int, int] a = {1: 1}\nb = copy(a)\nb[2] = 2\nprint(a)\nprint(b)"), "{1: 1}\n{1: 1, 2: 2}\n");
  CHECK_EQ(out("map[int, int] a = {1: 1}\nb = a\nb[5] = 5\nprint(a)"), "{1: 1, 5: 5}\n");
  CHECK_EQ(out("map[char, int] c = {}\nc['a'] = 1\nprint(c)"), "{'a': 1}\n");
  CHECK_EQ(out("print({})"), "{}\n");

  // ---- sets
  CHECK_EQ(out("set[int] s = {}\nadd(s, 3)\nadd(s, 1)\nadd(s, 3)\nprint(s)\nprint(len(s))\nprint(has(s, 1))\nremove(s, 1)\nprint(s)"), "{1, 3}\n2\ntrue\n{3}\n");
  CHECK_EQ(out("s = {5, 1, 5}\nprint(s)\nprint(first(s))\nprint(last(s))\nprint(keys(s))"), "{1, 5}\n1\n5\n[1, 5]\n");
  CHECK_EQ(out("for x in {3, 1, 2} {\n print(x)\n}"), "1\n2\n3\n");
  CHECK_EQ(out("set[string] s = {\"b\", \"a\"}\nprint(s)"), "{\"a\", \"b\"}\n");
  CHECK_EQ(err("set[int] s = {}\nadd(s, \"a\")"), "The value must be 'int', got 'string'.");
  CHECK_EQ(out("set[int] s = {}\nprint(first(s))"), "ERROR: first() on an empty collection.");

  // ---- heaps
  CHECK_EQ(err("heap[bool] h = {}"), "Heap elements must be int, float, string or char, got 'bool'.");
  CHECK_EQ(out("heap[int] h = {}\npush(h, 5)\npush(h, 1)\npush(h, 3)\nprint(top(h))\nprint(pop(h))\nprint(pop(h))\nprint(len(h))"), "1\n1\n3\n1\n");
  CHECK_EQ(out("maxheap[int] h = {}\npush(h, 5)\npush(h, 1)\npush(h, 3)\nprint(pop(h))\nprint(pop(h))"), "5\n3\n");
  CHECK_EQ(out("heap[string] h = {}\npush(h, \"b\")\npush(h, \"a\")\nprint(pop(h))"), "a\n");
  CHECK_EQ(out("heap[float] h = {}\npush(h, 2.5)\npush(h, 0.5)\nprint(pop(h))"), "0.5\n");
  CHECK_EQ(out("heap[int] h = {}\nfor x in [9, 4, 7, 1, 8, 2] {\n push(h, x)\n}\nint[] r = []\nwhile len(h) > 0 {\n push(r, pop(h))\n}\nprint(r)"), "[1, 2, 4, 7, 8, 9]\n");
  CHECK_EQ(out("heap[int] h = {}\npop(h)"), "ERROR: pop() on an empty heap.");
  CHECK_EQ(out("heap[int] h = {}\nprint(top(h))"), "ERROR: top() on an empty heap.");
  CHECK_EQ(err("heap[int] h = {}\npush(h, \"x\")"), "Cannot push a value of type 'string' into a heap of 'int'.");

  // ---- deques
  CHECK_EQ(out(R"IPX(deque[int] d = {}
push(d, 1)
push(d, 2)
push_front(d, 0)
print(d)
print(front(d))
print(back(d))
print(pop_front(d))
print(pop(d))
print(len(d))
d[0] = 9
print(d[0])
for x in d {
    print(x)
})IPX"), "[0, 1, 2]\n0\n2\n0\n2\n1\n9\n9\n");
  CHECK_EQ(out("deque[int] d = {}\nprint(pop_front(d))"), "ERROR: pop_front() on an empty deque.");
  CHECK_EQ(out("deque[int] d = {}\nprint(d[0])"), "ERROR: Index 0 is out of bounds for length 0.");
  CHECK_EQ(err("deque[int] d = {}\nx = pop_front(5)"), "pop_front() needs a deque, got 'int'.");

  // ---- structs
  CHECK_EQ(out(R"IPX(struct User {
    string name
    int age
}
u = User(name = "Ana", age = 20)
print(u)
print(u.name)
u.age += 1
print(u.age)
v = User("Bo", 30)
print(v)
w = User("Cy", age = 5)
print(w.age)
x = u
x.age = 99
print(u.age)
c = copy(u)
c.age = 1
print(u.age))IPX"),
           "User(name = \"Ana\", age = 20)\nAna\n21\nUser(name = \"Bo\", age = 30)\n5\n99\n99\n");
  CHECK_EQ(out(R"IPX(struct P {
    int x
    int y
}
P[] ps = []
push(ps, P(1, 2))
push(ps, P(x = 3, y = 4))
ps[0].x = 10
total = 0
for p in ps {
    total += p.x + p.y
}
print(total)
print(ps))IPX"), "19\n[P(x = 10, y = 2), P(x = 3, y = 4)]\n");
  CHECK_EQ(out(R"IPX(struct P {
    int x
    int y
}
fn mk(int a) -> P {
    return P(a, a * 2)
}
q = mk(4)
print(q.y))IPX"), "8\n");
  CHECK_EQ(out(R"IPX(struct T {
    int[] items
}
t = T(items = [])
push(t.items, 5)
print(t))IPX"), "T(items = [5])\n");
  CHECK_EQ(out(R"IPX(struct P {
    int x
    int y
}
map[string, P] m = {}
m["a"] = P(1, 2)
print(m["a"].y))IPX"), "2\n");
  CHECK_EQ(out(R"IPX(struct P {
    int x
    int y
}
struct L {
    P a
    P b
}
l = L(P(1, 2), P(3, 4))
l.b.x = 9
print(l.b.x)
print(l))IPX"), "9\nL(a = P(x = 1, y = 2), b = P(x = 9, y = 4))\n");
  CHECK_EQ(out(R"IPX(fn f(A a) -> int {
    return a.n
}
struct A {
    int n
}
print(f(A(7))))IPX"), "7\n");
  const char* P = "struct P {\n int x\n}\n";
  CHECK_EQ(err(std::string(P) + "p = P(x = \"a\")"), "Field 'x' of 'P' must be 'int', got 'string'.");
  CHECK_EQ(err(std::string(P) + "p = P(y = 1)"), "Struct 'P' has no field 'y'.");
  CHECK_EQ(err(std::string(P) + "p = P()"), "Missing field 'x' in P(...).");
  CHECK_EQ(err(std::string(P) + "p = P(1, 2)"), "Struct 'P' has only 1 field(s).");
  CHECK_EQ(err(std::string(P) + "p = P(1)\nx = p.z"), "Struct 'P' has no field 'z'.");
  CHECK_EQ(err(std::string(P) + "p = P(1)\np.x = \"s\""), "Cannot assign a value of type 'string' to field 'x' of type 'int'.");
  CHECK_EQ(err(std::string(P) + "p = P(1)\nq = P(2)\nx = p == q"), "Cannot compare values of type 'P' and 'P'.");
  CHECK_EQ(err(std::string(P) + "const K = P(1)"), "Constants cannot be collections or structs yet.");
  CHECK_EQ(err("x = 5\ny = x.a"), "Cannot access field 'a' on a value of type 'int'.");
  CHECK_EQ(err("struct P {\n P x\n}"), "A struct cannot contain itself directly.");
  CHECK_EQ(err("struct P {\n int x\n}\nstruct P {\n int y\n}"), "Struct 'P' is already defined.");
  CHECK_EQ(err("struct P {\n int x\n int x\n}"), "Field 'x' is declared twice.");
  CHECK_EQ(err("struct P {\n int x\n}\nfn P() {\n}"), "'P' is already defined as a struct.");
  CHECK_EQ(err("fn f(int x) -> int {\n return x\n}\ny = f(x = 1)"), "Named arguments can only be used to create a struct.");
  CHECK_EQ(err("struct S {\n map[float, int] m\n}"), "Map keys and set elements must be int, string, char or bool, got 'float'.");
}

static void testDiagnosticFormat() {
  std::string src = "name = \"Elisandro\"\nage = 20\nresult = name + age\n";
  auto d = check(src);
  CHECK(d.size() == 1);
  std::string f = formatDiagnostic(d[0], "main.ipa", src);
  CHECK(f.find("Ipangax Error") == 0);
  CHECK(f.find("main.ipa:3:10") != std::string::npos);
  CHECK(f.find("Cannot add values of type 'string' and 'int'.") != std::string::npos);
  CHECK(f.find("3 | result = name + age") != std::string::npos);
  CHECK(f.find("\n             ^^^^^^^^^^\n") != std::string::npos);
}

int main() {
  testLexer();
  testParser();
  testChecker();
  testInterpreter();
  testV02();
  testCompound();
  testControl();
  testBigPackage();
  testDiagnosticFormat();
  std::cout << (total - failures) << "/" << total << " checks passed\n";
  return failures ? 1 : 0;
}
