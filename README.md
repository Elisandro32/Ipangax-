<<<<<<< HEAD
# Ipangax

A programming language created by Elisandro Ipanga. Simple syntax, native performance as a
goal, safe by design as a goal. Inspired by Python, C++ and Rust.

This repository is at an early stage. Honest status:

| Component | Status |
|---|---|
| Language spec v0.1 (`docs/spec.md`) | Draft, matches the implementation |
| Lexer, parser, AST | Implemented |
| Type checker with diagnostics | Implemented |
| Tree-walking interpreter | Implemented (variables, const, if/else, while, for, do-while, match, ?:, break/continue, functions, recursion, arrays, strings, input()) |
| CLI `ipx run`, `ipx check`, `ipx version`, `ipx help` | Implemented |
| Tests | 353 checks, also clean under ASan/UBSan |
| Structs, maps, sets, heaps, deques, bit operators, math functions | Implemented |
| Enums, optional, result, tuples, modules, struct methods | Not implemented yet |
| `ipx build`, `ipx fmt`, `ipx repl` | Not implemented yet |
| IR, bytecode VM, LLVM backend | Not implemented yet |
| Web playground (Netlify Function running the real C++ interpreter) | Implemented, needs a Netlify deploy |
| WebAssembly build (run in the browser) | Not implemented yet |
| Website (home, docs, playground) | Implemented |
| VS Code extension, language server | Not implemented yet |

## Build

Requires a C++17 compiler (GCC or Clang) and make. No other dependencies.

```
make          # builds ./ipx
make test     # builds and runs the test suite
./ipx run examples/mvp.ipa
./ipx check examples/bad.ipa
echo "3 4" | ./ipx run examples/abc_product.ipa
```

## Speed (measured with -O2, one machine, tree-walking interpreter)

| Program | Time |
|---|---|
| 10 million loop iterations with an add and a modulo | about 0.4 s |
| DP over an array of 1 million elements | about 0.18 s |
| recursive fib(27), about 630 thousand calls | about 0.1 s |
| 1 million map updates | about 0.27 s |
| 500 thousand heap pushes and pops | about 0.42 s |

Variables are resolved to slots before running, which made loops about 3x faster than v0.2.
Rough comparison with C++ (estimate, not measured here): this interpreter is on the order of
15 to 50 times slower. It is enough for AtCoder-style problems with up to a few million
operations. Problems near 10^8 operations need a bytecode VM or a native backend, which do
not exist yet.

## Layout

```
compiler/    libipangax: lexer, parser, ast, semantic (checker), types, interpreter
             public API in compiler/ipangax.hpp (check, run)
cli/         ipx, a thin front-end over libipangax
tests/       test suite (no framework)
examples/    .ipa programs
docs/        language specification
website/     static site (HTML, CSS, one small JS file for the playground)
netlify/     serverless function /api/execute + static ipx binary (bin/)
netlify.toml Netlify configuration
runtime/ stdlib/ playground/ vscode-extension/   placeholders, Not implemented yet
compiler/ir, compiler/backend                             placeholders, Not implemented yet
```

All front-ends (CLI now, WebAssembly and language server later) are meant to call the same
`libipangax`, so there is a single grammar.

## Deploy on Netlify

Netlify Functions are not supported by drag-and-drop deploys, so use Git or the CLI.

1. Push this folder to a Git repository and import it on Netlify. Settings are read from
   `netlify.toml` (publish `website`, functions `netlify/functions`). No build command.
2. Or with the CLI: `npm i -g netlify-cli`, then `netlify login`, `netlify deploy --prod`.
3. Local test: `netlify dev`, then open the printed URL and use the Playground.

`netlify/functions/bin/ipx-linux-x64` is a static Linux x86-64 build of the interpreter.
Rebuild it after changing the compiler: `make static`.

Playground limits: 20 KB source, 100 KB input, 5 s, 256 KB output, 5 million statement steps,
call depth 1000, arrays up to 1 million elements, strings up to 10 million characters. Each run is a separate short-lived process in the Netlify
function sandbox. Opening `playground.html` straight from disk cannot run code.
=======
# Ipangax-
>>>>>>> 7d294d4d6679b4bb875d5a9c9fcceda0eb0a8057
