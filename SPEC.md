# Lux Language Specification

**Status:** draft 1.0, matching the `front` implementation  
**Date:** 2026-08-17

This document is the language definition. Tutorials and API walkthroughs live in [README.md](README.md). Where this spec and older comments disagree, this spec (and the compiler) win.

Lux is ready to freeze. The core is a complete scripting language. New work should be library, tools, and conformance — not more syntax.

---

## 1. Overview

Lux is a dynamically typed, garbage-collected scripting language. A program is compiled in one pass to bytecode and executed by a stack VM.

It started as Lox from *Crafting Interpreters* and keeps that shape: `var` / `fun` / `class`, lexical closures, single inheritance, and `print` as a statement. It adds arrays, `%`, `break` / `continue`, `and` / `or` / `&` / `|`, a ternary operator, `import`, and a large native library for files, HTTP, crypto, AWS, and optional databases.

### 1.1 Goals

- Small, readable syntax that a person can hold in their head
- Same language on Plan 9 and POSIX (macOS, Linux, OpenBSD)
- Scripting for automation, HTTP, and cloud work
- Failures as return values (`nil` / `false`), not exceptions

### 1.2 Non-goals (this version)

Do not add these without a new major version:

- Static types, generics, or a type checker as part of the language
- Exceptions / `try` / `catch`
- Threads inside one VM (HTTP workers are separate processes)
- Object / map literals (`{ key: value }` is not syntax)
- Anonymous functions / lambdas
- Pattern matching, `switch`, enums
- Bitwise operators (`&` and `|` are logical)
- `&&` / `||` (use `&` / `|` or `and` / `or`)
- Namespaced imports (`import` always dumps into globals)

### 1.3 What not to add

The language is already past the point where more syntax helps. Things that look tempting and should stay out:

| Tempting feature | Why not |
|---|---|
| `for (x in arr)` | A `while` plus index is enough; for-in is the only plausible small add, and it can wait |
| `{k: v}` map literals | `Dict`, class fields, and `parseJSON` already cover this |
| string interpolation | `"a" + x + "b"` is the defined style |
| `async` / promises | The server model is prefork processes |
| integer type | All numbers are IEEE 754 doubles |

If something is missing, it is almost always a **library function**, not a keyword.

---

## 2. Execution model

A Lux implementation:

1. Scans source to tokens
2. Compiles tokens to a function chunk (bytecode + constants)
3. Wraps that function in a closure and runs it on the VM

A **program** is a sequence of declarations. Top-level code is a script function. `return` is illegal at top level.

Hosts:

| Host | Entry |
|---|---|
| File | compile and run the file |
| REPL | compile and run each input |
| `-c` | compile and run one string |

Command-line arguments after the script path are available from `args()`. In the REPL, with `-c`, or with no extra arguments, `args()` is an empty array.

---

## 3. Lexical structure

Source is a sequence of Unicode/ASCII bytes. The scanner is byte-oriented. Identifiers are ASCII.

### 3.1 Whitespace and comments

Whitespace is space, tab, CR, and LF. Newlines increment the line number used in diagnostics.

Comments:

- Line comment: `//` through end of line
- Block comment: `/*` … `*/` (not nested)

### 3.2 Tokens

```
( ) { } [ ] , . - + ; / * % | & ? :
! != = == > >= < <=
identifier  string  number
```

Keywords (reserved, not usable as names):

```
and  break  class  continue  else  false  for  fun  if
import  nil  or  print  return  super  this  true  var  while
```

### 3.3 Identifiers

```
identifier  =  (letter | "_") { letter | digit | "_" }
letter      =  "A"…"Z" | "a"…"z"
digit       =  "0"…"9"
```

Names are case-sensitive.

### 3.4 Numbers

```
number  =  digits [ "." digits ]
digits  =  digit { digit }
```

There is no exponent form (`1e3`), no hex, and no leading-dot form (`.5` is invalid; write `0.5`).

Every number value is an IEEE 754 binary64 (`double`). There is no distinct integer type.

### 3.5 Strings

A string is `"…"` . Newlines inside the quotes are allowed and become part of the value.

Escape sequences (backslash is consumed by the scanner so `"\""` can close correctly):

| Source | Value |
|---|---|
| `\"` | `"` |
| `\\` | `\` |
| `\n` | LF |
| `\t` | TAB |
| `\r` | CR |
| `\` + any other char | that backslash and that char, unchanged |

Unknown escapes are **not** errors.

Strings are interned: two strings with the same bytes are the same object.

### 3.6 Other

`;` terminates statements. `{` starts a **block**, never an object literal. `}` closes a block.

---

## 4. Values and types

A value is one of:

| Kind | Literals / construction | `typeof(v)` |
|---|---|---|
| nil | `nil` | `"nil"` |
| boolean | `true`, `false` | `"bool"` |
| number | numeric literal | `"number"` |
| string | `"…"` | `"string"` |
| array | `[e1, e2, …]` | `"array"` |
| Float64Array | `float64_new(n)` | `"float64array"` |
| class | `class Name { … }` | `"class"` |
| closure | `fun` / method | `"closure"` |
| native function | host | `"native"` |
| bound method | `instance.method` (not called) | `"bound_method"` |
| instance | `Class(…)` | the class name, or `"object"` |

`typeof` of a user instance is the class name string.

### 4.1 Truthiness

Only `false` and `nil` are falsy. `0`, `""`, and `[]` are truthy.

### 4.2 Equality (`==` / `!=`)

- Numbers: IEEE equality. `NaN != NaN`.
- Otherwise: identity of the tagged value. Interned strings with equal content compare equal. Arrays and instances compare by **identity**, not deep structure.

### 4.3 Conversion to string

Used by `print`, `+` (when not both numbers or both arrays), and `valueToString`:

- `nil` → `"nil"`
- bool → `"true"` / `"false"`
- number → decimal text
- objects → implementation-defined printable form

---

## 5. Grammar

Informal EBNF. `declaration` is the start symbol for a chunk.

```
program        =  { declaration } EOF

declaration    =  classDecl | funDecl | varDecl | statement

classDecl      =  "class" IDENTIFIER [ "<" IDENTIFIER ] "{" { method } "}"
method         =  IDENTIFIER "(" parameters? ")" block
funDecl        =  "fun" IDENTIFIER "(" parameters? ")" block
parameters     =  IDENTIFIER { "," IDENTIFIER }
varDecl        =  "var" IDENTIFIER [ "=" expression ] ";"

statement      =  exprStmt | forStmt | ifStmt | printStmt | returnStmt
               |  whileStmt | breakStmt | continueStmt | importStmt | block

block          =  "{" { declaration } "}"
exprStmt       =  expression ";"
printStmt      =  "print" expression ";"
returnStmt     =  "return" [ expression ] ";"
breakStmt      =  "break" ";"
continueStmt   =  "continue" ";"
importStmt     =  "import" STRING ";"
ifStmt         =  "if" "(" expression ")" statement [ "else" statement ]
whileStmt      =  "while" "(" expression ")" statement
forStmt        =  "for" "(" ( varDecl | exprStmt | ";" )
                          [ expression ] ";"
                          [ expression ] ")" statement

expression     =  assignment
assignment     =  ( call "." IDENTIFIER | call "[" expression "]" | IDENTIFIER )
                  "=" assignment
               |  logic_or [ "?" assignment ":" assignment ]
logic_or       =  logic_and { ( "or" | "|" ) logic_and }
logic_and      =  equality  { ( "and" | "&" ) equality }
equality       =  comparison { ( "!=" | "==" ) comparison }
comparison     =  term { ( ">" | ">=" | "<" | "<=" ) term }
term           =  factor { ( "-" | "+" ) factor }
factor         =  unary { ( "/" | "*" | "%" ) unary }
unary          =  ( "!" | "-" ) unary | call
call           =  primary { "(" arguments? ")" | "." IDENTIFIER | "[" expression "]" }
primary        =  "true" | "false" | "nil" | "this" | NUMBER | STRING
               |  IDENTIFIER | "(" expression ")" | "super" "." IDENTIFIER
               |  "[" [ expression { "," expression } [ "," ] ] "]"
arguments      =  expression { "," expression }
```

`print` is a **statement**, not an expression. `fun` expressions (lambdas) do not exist; only `fun` declarations and methods.

---

## 6. Operators

Precedence, low to high:

| Level | Operators | Associativity |
|---|---|---|
| assignment / ternary | `=`  `? :` | right |
| or | `or`  `\|` | left |
| and | `and`  `&` | left |
| equality | `==`  `!=` | left |
| comparison | `<`  `>`  `<=`  `>=` | left |
| term | `+`  `-` | left |
| factor | `*`  `/`  `%` | left |
| unary | `!`  `-` | right |
| call | `()`  `.`  `[]` | left |

### 6.1 Arithmetic

`-` (unary and binary), `*`, `/`, `%` require numbers. Otherwise a runtime error.

`/` is IEEE division (division by zero yields Inf / NaN, not a Lux error).

`%` is floating modulo: `a - trunc(a / b) * b` (`trunc` toward zero via C `(long)`).

### 6.2 `+`

1. Both numbers → numeric add
2. Both arrays → new array, concatenation (operands unchanged)
3. Otherwise → convert both to string and concatenate

Repeated `arr + [x]` in a loop is O(n²). Prefer `arr[i] = x`.

### 6.3 Logical `and` / `or` / `&` / `|`

Short-circuit. They yield the **operand value**, not a coerced boolean:

- `and` / `&`: if left is falsy, result is left; else right
- `or` / `|`: if left is truthy, result is left; else right

`&` / `|` are **not** bitwise. `&&` / `||` are not tokens.

### 6.4 Ternary

`cond ? a : b` evaluates `cond`, then exactly one of `a` or `b`.

### 6.5 Call and property

`callee(args)` : at most 255 arguments. User functions have fixed arity; a mismatch is a runtime error. Natives decide arity themselves (wrong arity typically returns `nil`).

`obj.field` on an instance reads a field, else binds a method. Missing field and missing method is a runtime error.

`arr.length` and `float64.length` are the only properties on those types.

`obj.field = expr` writes an instance field (creates it if needed).

### 6.6 Indexing

`a[i]` : `i` must be a number, truncated toward zero as C `(int)`.

- Array: bounds error if `i < 0` or `i >= length`
- String: returns a one-byte string; same bounds rule; assignment `s[i] = …` is illegal
- Other targets: runtime error

`a[i] = v` : `a` must be an array. Negative `i` is an error. If `i >= length`, the array **grows**; holes are filled with `nil`.

---

## 7. Variables and scope

`var name;` initializes to `nil`. `var name = expr;`

Block `{ … }` introduces a scope. Locals shadow outer names.

A local cannot be read in its own initializer (`var a = a;` is an error).

Globals live in a process-wide table. `fun` and `class` declarations bind globals (or locals, if nested `fun` inside a block — nested `fun` is a local function). `class` is only a declaration, not nested as an expression.

Maximum **255 locals** per function (slot 0 is `this` or the empty name for functions). Maximum **255 upvalues** per function.

---

## 8. Control flow

`if (c) S` / `if (c) S else S2` — `c` uses truthiness.

`while (c) S`

`for (init; cond; incr) S` — any clause may be empty. `init` may be `var`.

`break` and `continue` are legal only inside a loop. `continue` jumps to the increment of `for`, or the condition of `while`. At most 256 `break`s pending in one loop.

Bodies may be a single statement. `luxlint` warns if that statement is not a block.

---

## 9. Functions and closures

```
fun name(p1, p2) {
  return p1 + p2;
}
```

At most 255 parameters. Missing `return` yields `nil`, except `init` methods, which always return `this`.

Nested `fun` captures enclosing locals (upvalues). Captured locals live on the heap after the outer frame returns.

Functions are first-class values (closures).

---

## 10. Classes

```
class Point {
  init(x, y) {
    this.x = x;
    this.y = y;
  }
  mag() {
    return sqrt(this.x * this.x + this.y * this.y);
  }
}

class Dot < Point {
  init(x, y, label) {
    super.init(x, y);
    this.label = label;
  }
}
```

- Single inheritance: `class Child < Parent`
- A class cannot inherit from itself
- `init` is the constructor; `Class(args)` constructs an instance and calls `init` if present
- `this` is valid only inside methods
- `super.method(…)` is valid only in a subclass
- `return` with a value is illegal in `init`
- Fields are stored on the instance; methods on the class (inherited along the superclass chain)
- No access modifiers, no `static`, no interfaces

`Res` and `Dict` / `Server` are native classes provided by the host (see §14).

---

## 11. Modules

```
import "utils/math.lux";
```

- Path is a string literal, relative to the **process working directory**, not the importing file
- The file is compiled and executed in the **same global environment**
- A path is loaded at most once per process (cached). Circular imports skip the already-marked file
- There is no `export`, `as`, or qualified name. Everything public is global

---

## 12. Errors

### 12.1 Compile errors

Printed as `[line N] Error …`. Compilation fails; the chunk is not run.

### 12.2 Runtime errors

Printed with a stack trace. The process (or `-c` / REPL entry) stops. Examples: type errors on `-` `*` `/` `%`, bad index, arity mismatch, missing property, failed `import` I/O.

### 12.3 Soft failures

Most natives return `nil` or `false` on I/O, HTTP, parse, or AWS failure. Scripts must test the result. There is no exception type.

`assert(cond, message)` : if `cond` is falsy, the host reports failure (used by `luxtest`).

---

## 13. Implementation limits

These are part of the current language as implemented:

| Limit | Value |
|---|---|
| Unique identifiers per chunk (globals / property names / `super` names) | 256 (`OP_*` operand is one byte) |
| Array literal elements | 255 |
| Call arguments | 255 |
| Function parameters | 255 |
| Locals per function | 256 slots (incl. slot 0) |
| Upvalues per function | 256 |
| Call frames | 64 |
| VM stack | `64 * 256` values |
| `break` patch list per loop | 256 |
| Constant pool | 24-bit (`OP_CONSTANT_LONG`); not limited to 256 |

Older docs said “256 constants per file.” That applied to identifier operands, not the constant pool. Large files can still fail on **too many unique names**.

---

## 14. Standard library

Natives are globals. Signatures below are the contract. Exact platform behavior (TLS, DNS order, SQL drivers) is host-specific; see README / BUILD.md.

Wrong arity or type usually returns `nil` (math domain errors follow C `math.h`, including NaN/Inf).

### 14.1 Core

| Name | Signature | Notes |
|---|---|---|
| `assert` | `assert(cond, message)` | Abort on falsy `cond` |
| `clock` | `clock()` → number | Process-relative seconds |
| `epoch` | `epoch()` → number | Unix time, UTC seconds |
| `exit` | `exit([code])` | Never returns; default `0` |
| `args` | `args()` → array | Strings after script path |
| `help` | `help()` / `help(name)` | REPL documentation |
| `typeof` | `typeof(value)` → string | See §4 |
| `len` | `len(value)` → number \| nil | String bytes; array count; Float64Array length |

### 14.2 Math

`floor`, `abs`, `ceil`, `sqrt`, `log`, `sin`, `cos` : one number.  
`pow(base, exp)` : two numbers.

### 14.3 Files and process

| Name | Result on failure |
|---|---|
| `readFile(path)` → string | `nil` |
| `writeFile(path, content)` → bool | `false` |
| `appendFile(path, content)` → bool | `false` |
| `deleteFile(path)` → bool | `false` |
| `fileExists(path)` → bool | `false` |
| `createDir(path)` → bool | `false` |
| `listDir(path)` → string | `nil` (newline-separated names) |
| `run(cmd)` → string | `nil` |

### 14.4 Strings and arrays

| Name | Notes |
|---|---|
| `strFind(haystack, needle, [start])` | Index or `-1` |
| `strSlice(s, start, end)` | Half-open; clamps |
| `strStartsWithAt(s, prefix, offset)` | bool |
| `strTrim(s)` | ASCII space/tab/LF/CR |
| `strSplit(s, delim)` | Empty delim → characters |
| `arrayIndexOf(arr, value)` | First `==`, or `-1` |
| `arrayContains(arr, value)` | bool |
| `arraySort(arr)` | In place; all-number or all-string; else `nil` |
| `arrayBinarySearch(arr, value)` | Sorted array; index or `-1` |

### 14.5 Data formats

| Name | Notes |
|---|---|
| `parseJSON(text)` | Objects → instances; arrays → arrays; failure → `nil` |
| `toJSON(value)` | Instances serialize fields; `Dict` storage is **not** those fields |
| `parseCSV(text, [sep])` | Array of row arrays; default comma |
| `getField(obj, name)` | Field by string name, or `nil` |
| `parseXml(text, tag)` | Array of matching tag contents |

There is **no** `{ "k": v }` literal. Build objects with classes / `Dict`, or parse JSON.

### 14.6 Float64Array

`float64_available`, `float64_new`, `float64_get`, `float64_set`, `float64_dot`, `float64_fill`, `float64_copy`, `float64_slice`, `float64_sum`, `float64_mean`, `float64_min`, `float64_max`, `float64_add`, `float64_mul`, `float64_axpy`.

### 14.7 Templates and Markdown

`renderTemplate(path, ctx)` — `ctx` is an instance. `{{ key }}`, `{% for x in items %}`, `{% include %}`, `{% include_md %}`.  
`markdownToHtml(md)`, `renderMarkdown(path)`.

### 14.8 HTTP and network

| Name | Returns |
|---|---|
| `httpGet(url)` | Body string or `nil` |
| `httpPost(url, body)` | Body string or `nil` |
| `httpPut(url, body)` | Body string or `nil` |
| `httpRequest(method, url, body, headers)` | Body string or `nil` |
| `httpServer(port)` | Legacy blocking server |
| `netLookup(host)` | First IP string or `nil` |
| `netPing(host, port)` | RTT ms, or `-1` |

Client calls return a **body string**, not a response object (no `.statusCode`).

### 14.9 Crypto and AWS

`sha256`, `hmacSha256`, `awsSignRequest`, `getAwsTimestamp`,  
`s3ListObjects`, `s3GetObject`, `s3PutObject`.

See [AWS_API_GUIDE.md](AWS_API_GUIDE.md) for parameter lists.

### 14.10 Database (POSIX, opt-in)

`dbConnect(driver, connection)`, `dbQuery(conn, sql)`, `dbClose(conn)`.  
May be absent in a given build.

### 14.11 Native classes

**`Dict`** — string → string map in native storage.

`Dict()`, `.put(k, v)`, `.get(k)`, `.has(k)`, `.remove(k)`, `.size()`, `.clear()`, `.iter()` (array of `{key, value}` instances).  
`toJSON` / `res.json` do **not** serialize native entries; use `.iter()` or build JSON yourself.

**`Server`** — `Server(port)` then:

`.get(path, handler)`, `.post(path, handler)`, `.getHost(host, path, handler)`, `.postHost(host, path, handler)`, `.vhost(host, root)`, `.static(root)`, `.use(middleware)`, `.workers(n)`, `.start()`.

Handlers are `fun (req, res) { … }`. Middleware is `fun (req, res, next) { …; next(); }`.

**`Res`** (second argument): `.send(text)`, `.html(html)`, `.json(value)`, `.status(code)`.

`req` fields include at least `method`, `path`, `body`, `host` (port stripped). `server.workers(n)` preforks 1…32 processes (default 4). Memory is not shared across workers after fork.

---

## 15. Library modules (`lib/`)

Not natives. Import by path from the working directory:

- `lib/dataframe.lux` — CSV / DataFrame
- `lib/graph.lux` — graphs and Graphviz DOT
- `lib/aws.lux` — AWS helpers

These are ordinary Lux source. They are not keywords.

---

## 16. Host tools

Not part of program syntax. Documented here so a distribution is complete:

| Tool | Role |
|---|---|
| `lux` / `8.out` | Interpreter |
| `luxfmt` | Formatter |
| `luxcheck` | Syntax check |
| `luxlint` | Unused vars, unreachable code, duplicate functions, shadowed globals |
| `luxtest` | Test runner (`assert`) |

---

## 17. Conformance

A conforming implementation:

1. Accepts the grammar in §5
2. Implements values, operators, classes, and `import` as specified
3. Provides the natives in §14 **or** documents which are omitted (databases, Float64, HTTP)
4. Honors truthiness, equality, and the error model
5. May differ on OS-level details (`run`, DNS order, TLS, file permissions)

The `tests/` tree is the de facto conformance suite. New language changes need tests there.

---

## 18. Relation to Lox

Lux is a Lox superset for the original statements and object model, plus:

- Arrays and indexing
- `%`, `break`, `continue`
- `&` `|` `and` `or` (Lox had `and` / `or` only)
- Ternary `? :`
- `import`
- Native library and `Dict` / `Server`
- String escapes and block comments
- `print` remains a statement, as in Lox
