# Ipangax language specification, v0.5 (draft)

Status: this document describes what the reference implementation in `compiler/`
does today. Anything not listed here is **Not implemented yet**.

## Source files

- Extension `.ipa`, UTF-8 text.
- Statements end at a new line. No semicolons. A new line inside `( )` does not end a statement.
- Comments: `//` to the end of the line.
- Blocks use `{ }`. `} else {` must be on one line.

## Types

`int` (64-bit signed), `float` (64-bit IEEE), `string`, `bool`, `char` (one byte).
There are no implicit conversions. Use `int(x)`, `float(x)`, `string(x)`.

| Conversion | Accepts |
|---|---|
| `int(x)` | int, float (truncates), char (its code), string (parsed, runtime error if invalid) |
| `float(x)` | int, float, string |
| `string(x)` | any value |

## Variables

- `x = expr` declares `x` if no visible variable has that name, otherwise it assigns.
  The type is inferred from the expression and fixed for the lifetime of the variable.
- `type x = expr` always declares and requires an initializer.
- `const NAME = expr` is immutable. The initializer must be a constant expression
  (literals, other constants, operators; no calls).
- Variables are mutable by default.
- Scope: each `{ }` block is a scope. A variable first assigned inside a block lives in that block.
- Shadowing: declaring a name again in the same scope is an error. A typed declaration
  in an inner scope may shadow an outer variable.
- Functions see their parameters, their own locals and top-level `const`s. They cannot read
  or modify top-level non-const variables (error). Pass values as parameters.

## Operators

Precedence, low to high: `? :`, `||`, `&&`, `== !=`, `< <= > >=`, `|`, `^`, `&`, `<< >>`, `+ -`, `* / %`, unary `- ! ~`.
Bit operators bind tighter than comparisons, so `x & 1 == 0` means `(x & 1) == 0`.
All binary operators are left-associative.

Bit operators `& | ^ ~ << >>` work on `int` only. A shift count must be 0 to 63 and a left shift
that loses bits is an overflow error. `>>` is an arithmetic shift.

Compound assignment: `x += e`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` work on variables and array elements
(`a[i] += e`). They mean `x = x op e`, so the same type rules apply. In `a[i] += e` the index
is evaluated twice, so it may not contain a function call (error).

- `+ - * /` need two `int` or two `float`. `+` also joins two `string`s.
- `%` needs two `int`. Integer `/` truncates toward zero.
- Comparisons need equal types. `==`/`!=` work on any type, ordering on int, float, string, char.
- `&& || !` need `bool` and short-circuit.

## Control flow

```
if cond { ... } else if cond { ... } else { ... }
while cond { ... }
```

Conditions must be `bool`. There is no truthiness.

```
for item in items { ... }       // arrays (each element) and strings (each char)
for i in range(n) { ... }       // 0 .. n-1
for i in range(a, b) { ... }    // a .. b-1
for i in range(a, b, step) { ... }   // step may be negative, never zero
break
continue
```

`range` is only valid as the iterable of a `for` and does not build an array.
The loop variable is new in every iteration and lives only inside the loop body.
`break` and `continue` are only valid inside a loop.

## match, do-while and the ternary operator

```
match day {
    1 { print("Monday") }
    2, 3 { print("Tuesday or Wednesday") }
    else { print("other") }
}

do {
    tries += 1
} while tries < 3

label = n % 2 == 0 ? "even" : "odd"
```

- `match` compares the value with each arm in order using `==` and runs the first arm that
  matches. There is no fall-through. The value must be `int`, `string`, `char` or `bool`
  (not `float`), and every arm must have the same type. An arm lists one or more
  expressions separated by commas. `else` is optional and must be last. If nothing matches and
  there is no `else`, nothing runs. `break` and `continue` inside an arm affect the
  enclosing loop. Each arm is its own scope.
- `do { } while cond` runs the block once, then repeats while `cond` is true. `break` and
  `continue` work as in other loops. Variables declared in the block are not visible in the
  condition.
- `c ? a : b` needs a `bool` condition and two branches of the same type. Only the chosen
  branch is evaluated. It is right-associative: `a ? 1 : b ? 2 : 3`.
- There is no C-style `for (;;)`. Use `for i in range(a, b, step)` or `while`.

## Arrays

```
int[] a = [5, 3, 8]
b = [1, 2, 3]           // inferred int[]
grid = [[1, 2], [3, 4]] // int[][]
a[0] = 10
print(a[1])
```

- All elements have the same type. `[]` needs a declared type: `int[] a = []`.
- Indexing is bounds-checked. Out of range or negative indexes are runtime errors.
- Arrays are shared references (like Python lists): `b = a` makes both names point to the same
  array, and a function that receives an array can change it. Use `copy(a)` for a deep copy.
- Arrays cannot be `const`, and cannot be compared with `==` yet.
- Maximum length is 10 million elements.

## Strings and input

- `len(s)`, `s[i]` (a `char`), `for c in s`. Strings are immutable.
- `input()` returns the next line of standard input without the line break and fails with
  a runtime error when there is no more input.
- `split(s)` splits on whitespace and returns `string[]`. `join(parts, sep)` joins a `string[]`.

```
n = int(input())
nums = split(input())
```

## Built-in functions

| Function | Result |
|---|---|
| `print(x)` | prints any value and a new line |
| `input()` | `string`, next input line |
| `len(x)` | `int`, length of a string or array |
| `push(a, v)` | appends to an array |
| `pop(a)` | removes and returns the last element |
| `copy(a)` | deep copy |
| `make(n, v)` | array of `n` copies of `v` (deep copies for nested arrays) |
| `sort(a)` | sorts an array of int, float, string, char or bool in place |
| `split(s)`, `join(a, sep)` | see above |
| `min(a, b)`, `max(a, b)` | two ints or two floats |
| `abs(x)` | int or float |
| `pow(a, b)` | two ints (checked for overflow, exponent not negative) or two floats |
| `sqrt(x)` | float only |
| `gcd(a, b)`, `lcm(a, b)` | ints, overflow checked |
| `powmod(a, b, m)` | `a^b mod m`, no overflow, `m > 0`, `b >= 0` |
| `floor(x)`, `ceil(x)`, `round(x)` | float to `int` (error if out of range) |
| `log(x)`, `log2(x)`, `log10(x)`, `exp(x)`, `sin(x)`, `cos(x)`, `tan(x)`, `atan2(y, x)` | floats |
| `PI`, `E` | predefined float constants (a program may define its own) |
| `has(c, x)` | map or set key test, or linear search in an array |
| `get(m, k, default)`, `remove(c, k)`, `add(s, x)` | map and set operations |
| `keys(c)`, `values(m)`, `first(c)`, `last(c)` | ordered views of a map or set |
| `top(h)` | smallest (heap) or largest (maxheap) element |
| `push_front(d, x)`, `pop_front(d)`, `front(d)`, `back(d)` | deque operations |
| `int(x)`, `float(x)`, `string(x)` | conversions |

User functions cannot reuse these names. A variable may, since variables and functions
live in different namespaces.

## Functions

```
fn add(int a, int b) -> int {
    return a + b
}

fn greet(string name) {
    print("Hello " + name)
}
```

- Declared at the top level only. Order does not matter. Recursion works.
- No overloading, no nested functions.
- A function with a return type must return on every path (checked).
- `fn main()` is optional. If present it runs after all top-level statements.
  `main` cannot take parameters or return a value in v0.1.
- Built-in: `print(value)` prints any value and a new line.

## Maps, sets, heaps and deques

```
map[string, int] count = {}          // ordered by key, like C++ std::map
count["a"] = 1
count["a"] += 1                      // the key must already exist
x = get(count, "z", 0)               // with a default
if has(count, "a") { }
for k in count { print(k) }          // keys in order
remove(count, "a")

set[int] seen = {}                   // ordered set
add(seen, 3)
s = {1, 2, 3}                        // literals: {k: v, ...} and {a, b, ...}

heap[int] h = {}                     // min-heap
maxheap[int] big = {}                // max-heap
push(h, 5)
smallest = pop(h)

deque[int] q = {}                    // queue, stack and deque
push(q, 1)                           // back
push_front(q, 0)
a = pop_front(q)
b = pop(q)                           // back
```

- `{}` is the empty literal for any map, set, heap or deque and needs a declared type.
  `x = {}` is an error.
- Keys and set elements must be `int`, `string`, `char` or `bool`. Heap elements must be
  `int`, `float`, `string` or `char`. Values and deque elements can be anything.
- Reading a missing key is a runtime error. Use `get` or `has`.
- Maps, sets, heaps and deques are shared references like arrays. `copy(c)` is a deep copy.
- `for` over a map or set goes through a snapshot of the keys in ascending order.
- Map and set literals must fit on one line. Maximum size is 10 million elements.
- No tuples or pairs yet: to put two numbers in a heap, pack them, for example
  `(dist << 10) | node`.

## Structs

```
struct User {
    string name
    int age
}

u = User(name = "Ana", age = 20)     // named arguments, any order
v = User("Bo", 30)                   // or positional, or both (positional first)
u.age += 1
print(u)                             // User(name = "Ana", age = 21)
User[] users = []
push(users, u)
users[0].age = 5
```

- Structs are declared at the top level, in any order, with one field per line.
- Every field must be given when constructing. There are no default values.
- Structs are shared references: `b = a` points to the same struct. Use `copy(a)`.
- A struct cannot contain itself directly. Use an array of its own type.
- Structs cannot be compared with `==`, used as map keys or declared `const` yet.
- No methods yet.

## Errors

Compile-time errors are reported with file, line, column, the source line and a caret.
All type errors found are reported, not only the first. Syntax and lexical errors stop at the first.

Runtime errors (with the same format): division by zero, integer overflow
(checked, never wraps), invalid conversion, index out of bounds, reading past the end of
input, maximum call depth (1000 by default).
The embedding API can also set a step limit (for the future playground).

## Memory model

Scalars and strings are copied. Arrays are reference-counted shared references; there is no
garbage collector. A struct that holds an array of its own type can form a reference cycle
(for example a tree node whose children array contains its parent); such objects are not
freed until the program ends. No raw pointers are used in the interpreter.

## Not implemented yet

Enums, optional, result, tuples and pairs, modules, struct methods, multiple assignment,
custom sort order, big integers, `ipx build`, `ipx fmt`, `ipx repl`, IR, bytecode VM, LLVM backend,
WebAssembly build, playground, website, VS Code extension, language server.

## Known limitations

- Because `x = value` declares on first use, a typo in an assignment creates a new variable.
  A future `ipx check` warning for never-read variables is planned (Not implemented yet).
- Integer overflow checks use `__builtin_*_overflow` (GCC/Clang).
- Columns count bytes, so lines with non-ASCII characters may place the caret slightly off.
