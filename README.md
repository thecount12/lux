# Lux — A Portable Scripting Language

Lux is a dynamically-typed scripting language for system automation, cloud integration, and embedded scripting. Originally written in Plan 9 C, it runs on **macOS, Linux, OpenBSD, and Plan 9**.

## 🌟 Why "Lux"?
The name **Lux** (Latin for "light") signifies the evolution of this project. While it maintains full parity with the original Lox specification, it has been "enlightened" with new features, better performance, and first-class support for the **Plan 9** operating system.

Built from first principles (inspired by "Crafting Interpreters"), Lux combines a clean language design with modern features: HTTP/HTTPS, cryptography, AWS integration, databases, and file I/O. The language itself is defined in [SPEC.md](SPEC.md).

## Features

**Language:**
- **Dynamic typing** — numbers, strings, arrays, hashes, objects
- **First-class functions** — lexical scoping, closures, recursion
- **Classes & inheritance** — single inheritance, methods, fields
- **Modules** — `import "path.lux"` for code organization
- **Automatic GC** — mark-and-sweep garbage collector

**Standard Library:**

- **File I/O** — read, write, append, list directories
- **JSON** — parse and serialize
- **XML** — basic parsing
- **Strings/Arrays** — slice, find, split, sort, binary search, array concatenation (`+`)
- **Dictionaries** — native `Dict` class with `put`/`get`/`has`/`remove`/`size`/`clear` plus `iter()` returning an array of `{key, value}` entries
- **Float64Array** — typed double buffer, reductions and dot product
- **DataFrame / CSV** — quoted CSV, groupBy, sortBy, `lib/dataframe.lux`
- **Graphs** — BFS/DFS and Graphviz DOT export, `lib/graph.lux`
- **Math** — abs, ceil, floor, sqrt, pow, log, sin, cos
- **HTTP** — client (GET/POST/PUT) and server (routes, static, virtual hosts, OpenAPI/Swagger UI, Lambda via `server.handle` / Mangum)
- **Network** — `netLookup` (DNS) and `netPing` (TCP-connect RTT), no `run()`
- **Templates** — native `renderTemplate` (`{{ }}`, `{% for %}`, `{% include %}`)
- **Crypto** — SHA-256, HMAC-SHA256, AWS request signing
- **Cloud** — AWS S3, STS, IAM operations
- **Databases** — SQLite, PostgreSQL, MySQL (opt-in)
- **Code formatting** — `luxfmt` (gofmt-style)
- **Testing** — `luxtest` with timeouts and reporting
- **Web checks** — `lib/webtest.lux` (POSIX): curl status/body, k6 load, Playwright (shells out; no VM change)
- **Assertions** — `assert(condition, message)` for test-driven development
- **Syntax** — `luxcheck` fast lightweight syntax checker
- **Linting** — `luxlint` static checks: unused vars, unreachable code, duplicate functions, shadowed globals

## Quick Start

### Build (macOS / Linux)
```bash
cd posix
make
./lux ../examples/demo.lux
```

### Build (OpenBSD)
```bash
cd posix
gmake  # GNU make required
./lux ../examples/demo.lux
```

### Build (Plan 9)
```bash
mk
./8.out ../examples/demo.lux
```

### Hello Lux
```lux
print "Hello, Lux!";

fun fib(n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

print fib(10);  // 55
```

### Run One-Liner
```bash
./lux -c "print 1 + 2;"              # POSIX: Quick eval (scripting, CI)
./lux -c "assert(len(\"hi\") == 2);" # Assertions for checks
./lux -c 'var out = run("echo hello"); print out;'  # POSIX: Subprocess
# Plan 9: 8.out -c 'print 1 + 2;'   (use single quotes; rc parses " differently)
# Plan 9: 8.out -c 'var out = run("echo hello"); print out;'  # Subprocess
```

### Run Tests
```bash
./luxtest -t 5 tests/    # All tests with 5s timeout
./luxtest tests/fib.lux  # Single test
```

### Format Code
```bash
./luxfmt --write myfile.lux   # In-place (POSIX)
./luxfmt -w myfile.lux        # In-place (Plan 9)

### Lint
```bash
./luxlint myfile.lux   # Warn on unused vars, unreachable code, duplicate functions, shadowed globals
# Plan 9: rc luxlint.rc  (build first)
```
Exit codes: 0 = no issues, 1 = warnings, 65 = compile error, 64 = usage error.

## Detailed Build Instructions

See [BUILD.md](BUILD.md) for comprehensive platform-specific build guides:
- **macOS** — Homebrew setup, x86_64 compilation
- **Linux** — Debian, Ubuntu, Fedora, CentOS
- **OpenBSD** — GNU Make, port package paths
- **Plan 9** — mk tool, compiler selection
- **Optional features** — SQLite, PostgreSQL, MySQL database drivers

## Why Lux?

**Lux is ideal for:**
- **System automation** — File I/O, process orchestration, configuration management
- **Cloud scripting** — Direct AWS S3/STS/IAM integration, HTTP/HTTPS clients
- **Embedded scripting** — Lightweight bytecode interpreter, Plan 9 native
- **Testing frameworks** — Built-in `assert()` and `luxtest` runner
- **Portable tools** — Single binary, runs on macOS, Linux, OpenBSD, Plan 9

**Lux is NOT ideal for:**
**Lux is NOT ideal for:**
- Large numerical compute — Lux is not tuned for large-scale number‑crunching; prefer numerical Python/Julia for heavy workloads. For moderate numeric work you can use Float64Array, BLAS wrappers, or build the POSIX runtime with optimizations (see `posix/Makefile` and `benchmark.md`).
- GUI development (no graphics APIs)
- Mobile development (not designed for mobile targets)

## Practical Examples

### HTTP Client
```lux
// GET — returns response body string, or nil on failure
// Single object endpoint (property access works on all builds):
var res = httpGet("https://fakestoreapi.com/products/1");
if (res != nil) {
    var data = parseJSON(res);
    if (data != nil) {
        print data.title;
        print data.price;
    }
}

// Array endpoint (requires lux built with JSON→native-array parseJSON):
// var list = parseJSON(httpGet("https://fakestoreapi.com/products"));
// print list[0].title;

// POST with custom headers (httpRequest also returns body string or nil)
class Headers { init() {} }
var headers = Headers();
headers.Authorization = "Bearer token123";
headers.Content_Type = "application/json";

class User {
    init(username, email) {
        this.username = username;
        this.email = email;
    }
}
var body = toJSON(User("alice", "alice@example.com"));
var resp = httpRequest("POST", "https://api.example.com/users", body, headers);
if (resp != nil) {
    print "User created!";
}

// Multipart POST (curl -F). httpPost() is JSON-only — use Form + httpRequest.
import "../lib/http.lux";
var form = Form();
form.field("name", "legacy");
form.file("file", "script.js", "application/x-javascript");
var formHeaders = Headers();
formHeaders.accept = "application/json";
var formResp = httpRequest("POST", "https://api.example.com/upload", form, formHeaders);

```

### HTTP Server
```lux
// Configurable Server: routes, static, multi-domain vhosts (see examples/vhost_server.lux)
import "../lib/swagger.lux";
var server = Server(8080);
server.workers(4);
server.vhost("example.com", "public/example");
server.get("/health", handleHealth, Op("Health check", "ops"));
swagger(server);   // GET /docs
server.start();

// Same routes on AWS Lambda (Mangum-style) — no listen/bind:
// var page = server.handle("GET", "/health");
// Mangum(server);   // lib/mangum.lux + lambda/handler.lux

// Legacy one-liner with built-in routes:
httpServer(8080);
// Handles GET /, GET /echo, POST /echo automatically
```

### File I/O
```lux
// Read entire file
var content = readFile("input.txt");
print content;

// Write file
writeFile("output.txt", "Hello, Lux!");

// Append to log
appendFile("events.log", "Timestamp: " + epoch() + "\n");

// List directory
var files = listDir(".");
print files;

// Check existence
if (fileExists("config.json")) {
    var config = parseJSON(readFile("config.json"));
}
```

### Subprocess
```lux
var out = run("echo hello");
print out;  // hello

// With file I/O
var lines = run("wc -l config.json");
print lines;
```

### Network (DNS + TCP ping)
```lux
var ip = netLookup("example.com");
if (ip != nil) print ip;            // first resolved address, or nil

var ms = netPing("example.com", 443);
if (ms >= 0) print ms;              // TCP-connect round-trip milliseconds
else print "unreachable";           // -1 on timeout / refused / bad args
```

### JSON Data
```lux
var user = {
    "name": "Alice",
    "age": 30,
    "tags": ["admin", "dev"]
};

// Serialize
var json = toJSON(user);
writeFile("user.json", json);

// Parse
var data = parseJSON(readFile("user.json"));
print data["name"];     // Alice
print data["tags"][0];  // admin
```

### Cryptography
```lux
// SHA-256 hash
var hash = sha256("password123");

// HMAC-SHA256  
var signature = hmacSha256("secret", "message");

// AWS request signing
var sig = awsSignRequest("us-east-1", "s3", "AWS4-HMAC-SHA256", 
                         "20250101T000000Z", "...");
```

### Testing with Assertions
```lux
// Run: ./luxtest script.lux

assert(1 + 1 == 2, "basic math");
assert(len([1, 2, 3]) == 3, "array length");
assert("hello".startsWith("hel"), "string prefix");

class Calculator {
    add(a, b) { return a + b; }
}
var calc = Calculator();
assert(calc.add(2, 3) == 5, "calculator.add");

print "All assertions passed!";
```

## Language Features

### Basic Types
- **Numbers**: `42`, `3.14` (all numbers are doubles)
- **Strings**: `"hello"`, `"world"`
- **Booleans**: `true`, `false`
- **Nil**: `nil`

#### Multi-Line String Literals
String literals can span multiple lines in source code. Newlines are preserved as-is in the string value:

```lux
// String with embedded newlines
var message = "Line 1
Line 2
Line 3";
print message;
// Output:
// Line 1
// Line 2
// Line 3

// Works in return statements
fun getMultiLine() {
    return "foo
  bar";
}
print getMultiLine();
// Output:
// foo
//   bar

// Also works directly in print
print "Hello
World";
// Output:
// Hello
// World
```

**Note**: Lux does not support escape sequences like `\n` or `\t` - use actual newlines in your source code instead.

### Variables
```lux
var x = 10;
var name = "Alice";
var flag = true;
```

### Control Flow
```lux
// If/else
if (x > 5) {
    print "big";
} else {
    print "small";
}

// While loop
while (x > 0) {
    print x;
    x = x - 1;
}

// For loop
for (var i = 0; i < 10; i = i + 1) {
    print i;
}

// break / continue
for (var i = 0; i < 10; i = i + 1) {
    if (i == 2) continue;  // skip this iteration
    if (i == 5) break;     // leave the loop
    print i;
}
```

### Logical operators and ternary

- `|` — logical OR (short-circuit). Example: `if (a | b) { ... }`
- `&` — logical AND (short-circuit). Example: `if (a & b) { ... }`
- Ternary conditional: `cond ? exprTrue : exprFalse` returns a value and can be used inside expressions.

`and` / `or` keywords are aliases for `&` / `|`. These are logical (short-circuit) operators, not bitwise. `||` and `&&` are not currently aliases.

### Functions
```lux
fun greet(name) {
    return "Hello, " + name;
}

print greet("World");

// Closures
fun makeCounter() {
    var count = 0;
    fun increment() {
        count = count + 1;
        return count;
    }
    return increment;
}



var counter = makeCounter();

print counter();  // 1
print counter();  // 2

```

### Classes and Objects
```lux
class Person {

    init(name, age) {
        this.name = name;

        this.age = age;
    }

    greet() {
        print "Hi, I'm " + this.name;

    }
}


var alice = Person("Alice", 30);
alice.greet();
```


### Inheritance
```lux

class Animal {
    init(name) {
        this.name = name;

    }


    speak() {
        print this.name + " makes a sound";
    }

}


class Dog < Animal {
    speak() {
        print this.name + " barks!";
    }
}

var buddy = Dog("Buddy");
buddy.speak();  // Buddy barks!
```

### Arrays
```lux
// Create arrays with bracket syntax
var numbers = [1, 2, 3, 4, 5];
var names = ["Alice", "Bob", "Charlie"];
var mixed = [42, "hello", true, nil];

// Access elements by index (0-based)
print numbers[0];     // 1
print names[2];       // Charlie

// Modify existing elements
numbers[0] = 100;
print numbers[0];     // 100

// Get array length
print numbers.length; // 5

// Iterate over arrays
var i = 0;
while (i < names.length) {
    print names[i];
    i = i + 1;
}

// Arrays grow dynamically when assigning past current length
var data = [1, 2, 3];
data[0] = 10;         // Overwrite existing index
data[5] = 50;         // Auto-grows array
print data.length;    // 6
print data[3];        // nil (gap values are filled with nil)

// Array concatenation with +
var a = [1, 2];
var b = a + [3];     // b is [1, 2, 3]

// Nested arrays
var matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
print matrix[1][2];   // 6

// Arrays from native functions
var xmlResponse = parseXml("<items><item>A</item><item>B</item></items>", "item");
// parseXml returns an array of matching tag contents
for (var j = 0; j < xmlResponse.length; j = j + 1) {
    print xmlResponse[j];
}
```

### Native Dictionary (`Dict`)
Lux now exposes a native `Dict` class for string→string maps. The class supports `put`, `get`, `has`, `remove`, `size`, `clear`, and `iter()`. Calling `iter()` returns an array of `{key, value}` objects that you can loop over from Lux.

```lux
var book = Dict();
book.put("alice", "{\"name\":\"Alice\",\"email\":\"alice@example.com\"}");
book.put("bob",   "{\"name\":\"Bob\",\"email\":\"bob@example.com\"}");

var entries = book.iter();
var json = "{";
var first = 1;
var i = 0;
while (i < entries.length) {
  var entry = entries[i];
  if (first == 0) { json = json + ","; }
  first = 0;
  json = json + "\"" + entry.key + "\":" + entry.value;
  i = i + 1;
}
json = json + "}";
print json;
```

The iterator is handy for rebuilding structured data or producing JSON output without importing helper libraries.

### Modules and Imports
```lux
// Import code from other files to organize your programs
// Paths are relative to the current working directory

// math_utils.lux:
fun add(a, b) {
    return a + b;
}

fun multiply(a, b) {
    return a * b;
}

// main.lux:
import "math_utils.lux";

print(add(5, 3));        // 8
print(multiply(4, 7));   // 28

// Features:
// - Imported files are executed once (cached to prevent duplicates)
// - All definitions go into global scope
// - Circular imports are prevented automatically
// - Import paths are relative to where you run the program
// - Each import is its own compiler chunk (helps the 256 identifier-slot limit; see below)

// Example directory structure:
// project/
//   main.lux
//   utils/
//     math.lux
//     string.lux

// From project/ directory:
import "utils/math.lux";
import "utils/string.lux";
```

## Building

### On Plan 9:
```
mk
```

### On POSIX systems (experimental):
```
cd posix/
make
```

Platform guides:
- Linux: `posix/README-LINUX.md`
- OpenBSD: `posix/README-OPENBSD.md`

## Running

### Interactive REPL:
```
8.out
```

### Run a file:
```
8.out script.lux
```

### Run one-liner (scripting / CI):
```
8.out -c 'print 1 + 2;'                    # Plan 9: use single quotes (rc shell)
8.out -c 'assert(len("hello") == 5);'      # CI-style checks
# POSIX: ./lux -c "print 1 + 2;"
```

### Script arguments:
Pass arguments after the script path. Inside the script, call `args()` to get them as an array of strings:

```
./lux script.lux foo bar
```

```lux
var a = args();
print a.length;  // 2
print a[0];      // foo
print a[1];      // bar
```

`args()` returns an empty array in the REPL, with `lux -c`, or when no extra arguments are given.

### Example files:
```
8.out examples/demo.lux                 # Simple working demo
8.out examples/tutorial_simple.lux      # Quick tutorial (recommended)
8.out examples/quickref.lux             # Language quick reference
8.out examples/test_concat.lux          # String concatenation examples
8.out tests/test_fileio.lux             # File I/O examples
8.out tests/test_fileops.lux            # Complete file operations test
8.out tests/test_json.lux               # JSON parsing and serialization
8.out tests/test_http_plan9.lux         # HTTP client (Plan 9 HTTP)
8.out tests/test_https.lux              # HTTPS client with TLS (Plan 9)
posix/lux tests/test_http.lux    # HTTP/HTTPS examples (POSIX)
8.out tests/test_http_server.lux        # HTTP server (both Plan 9 and POSIX)
posix/lux examples/lambda_web.lux       # Local HTTP server (Mangum if Lambda event file)
posix/lux examples/lambda_web.lux --once  # One request via server.handle, no listen
posix/lux examples/swagger_server.lux     # OpenAPI /docs (port 8085)
posix/lux examples/oauth_server.lux       # OAuth2 client-credentials (port 8086)
8.out examples/dataframe_prototype.lux  # DataFrame / CSV demo
8.out examples/graph_demo.lux           # Graph + Graphviz DOT
8.out tests/c29-inherit.lux             # Inheritance examples
8.out tests/closure.lux                 # Closure examples
```

### Compiler limit: 256 identifier slots per chunk

Compile error:

```
[line N] Error at 'name': Too many unique identifiers in one chunk.
```

This is **not** a file line-count limit. Each compiled function is a **chunk** (top-level script, each `fun`, each method, each `import`). Identifier opcodes (`assert`, `.status`, globals, …) can only point at constant-pool slots `0..255`. String literals share that same pool (they can use `OP_CONSTANT_LONG` for later slots, but they still take indices). Names are **not interned**: every `assert(...)` at top level burns another slot.

A test file with lots of `assert` + string literals hits this while a 400-line `lib/` file is fine, because methods each get their own chunk.

**Fix:** wrap the busy top-level code in a function (see `tests/test_webtest.lux`):

```lux
fun run() {
    assert(len("hi") == 2, "len");
    // more asserts ...
}
run();
```

Or split files / `import`. Full table: [SPEC.md](SPEC.md) §13.

## String and Array Concatenation

Lux supports **automatic type conversion** with the `+` operator:
- **Numbers**: `1 + 2` → `3` (numeric addition)
- **Arrays**: `[1, 2] + [3]` → `[1, 2, 3]` (array concatenation; both operands must be arrays)
- **Strings**: `"Hello" + " World"` → `"Hello World"`
- **Mixed types**: `"Score: " + 42` → `"Score: 42"` (auto-converts to string)
- **Multiple types**: `"Player " + 1 + " wins!"` → `"Player 1 wins!"`

Any type (number, boolean, nil) is automatically converted to string when used with `+` and at least one string operand. For building large arrays, prefer index assignment (`arr[i] = x`) over repeated concatenation, since `arr + [x]` in a loop copies the whole array each time (O(n²)).

## Built-in Functions

### REPL Help

Use `help()` in the REPL to list callable globals (native functions, classes, and functions).
Use `help("name")` to inspect one symbol.

```lux
help();
help("dbQuery");
help("str"); // unknown names can suggest close matches
print epoch(); // Unix epoch seconds (UTC)

class Greeter {
    init(name) {
        this.name = name;
    }
    hello() {
        print "hello " + this.name;
    }
}
help("Greeter"); // shows class type and known method names
```

`help()` groups callables by category (Core, Math, File and Directory, String and Array, Data Formats, Float64, HTTP, Network, Crypto, AWS, Database, and User or Other) so long lists are easier to scan.

`epoch()` returns Unix timestamp seconds in UTC, which is useful for durable event timestamps.

Plan 9 REPL tip: if multiline input is awkward in your terminal, define classes on one line and then inspect them.

```lux
class Greeter {init(name){this.name = name;} hello(){print "hello " + this.name;}}
help("Greeter");
```

Typical output for `help("dbQuery")`:

```text
dbQuery
    dbQuery(conn, sql)
    Execute SQL and return rows for queries.
    Type: native function
```

### Runtime Control

#### `exit([code])` → never returns
Terminate the Lux runtime immediately. The optional numeric `code` defaults to `0` (success); non-zero values signal failure and make `luxtest`/shell detect an error. On POSIX builds the process exit code matches `code`, while on Plan 9 the interpreter exits with reason `exit <code>`.

```lux
if (!fileExists("config.json")) {
    print "Missing config file";
    exit(1); // abort with failure
}

// Normal shutdown
exit();
```

### File Operations

Lux includes native file I/O functions for both Plan 9 and POSIX systems:

#### `readFile(path)` → string or nil
Reads the entire contents of a file and returns it as a string.
```lux
var content = readFile("data.txt");
if (content != nil) {
    print content;
} else {
    print "Failed to read file";
}
```

#### `File(path)` → instance
Opens a file for line-at-a-time reading. `init` always returns the instance; check `f.ok` after construction. `readLine()` returns the next line without the newline (trailing `\r` is stripped). An empty line is `""`. `nil` means EOF, a closed handle, an open failure, or a line longer than 32 MiB. `close()` releases the OS handle; the GC also closes the handle if you drop the instance.

```lux
var f = File("results.ndjson");
if (!f.ok) {
    print "cannot open";
} else {
    var line = f.readLine();
    while (line != nil) {
        var obj = parseJSON(line);
        line = f.readLine();
    }
    f.close();
}
```

Use `File` for large files. `readFile` still loads the whole file into one string.

NDJSON (one JSON object per line): stream with `File.readLine()`, or cache `strSplit` once — never re-read inside the loop. See [NDJSON.md](NDJSON.md).

#### `renderTemplate(path, ctx)` → string
Native HTML template renderer (Plan 9 and POSIX). `ctx` must be an **instance** whose fields supply values (e.g. a small class with `title` / `items`). Expanded `{% include %}` / `{% include_md %}` results are cached for the process lifetime — restart after editing templates or included Markdown.

| Syntax | Behavior |
|--------|----------|
| `{{ key }}` | Lookup `ctx.key`, HTML-escaped; missing → empty |
| `{% for x in items %}…{% endfor %}` | Non-nested loop over an array field |
| `{% include "file.tpl" %}` | Include relative to the current template’s directory |
| `{% include_md "file.md" %}` | Render Markdown to HTML and insert (unescaped tags) |

```lux
class TplCtx {
  init(title, items) {
    this.title = title;
    this.items = items;
  }
}
var html = renderTemplate("public/index.tpl", TplCtx("Hi", ["a", "b"]));
res.html(html);
```

See `examples/template_server/server.lux` and `tests/test_template.lux`.

#### `markdownToHtml(md)` → string
Convert a Markdown string to an HTML fragment (Plan 9 and POSIX). Supports headings (`#`–`######`), paragraphs, `*emphasis*` / `**strong**`, `[text](url)`, unordered/ordered lists, fenced code blocks, and inline `` `code` ``. Text is HTML-escaped. `javascript:` URLs are not turned into links.

```lux
var html = markdownToHtml("# Hi\n\nHello *world*.");
res.html(html);
```

#### `renderMarkdown(path)` → string
Read a Markdown file and convert it with `markdownToHtml`.

```lux
var html = renderMarkdown("notes/todo.md");
res.html(html);
```

Quick local preview of a directory of `.md` files:

```text
./lux examples/mdview.lux examples/mdview
# Plan 9: 8.out examples/mdview.lux examples/mdview
# browse http://127.0.0.1:8090/
```

See `examples/mdview.lux`, `examples/mdview/sample.md`, and `tests/test_markdown.lux`.

#### `writeFile(path, content)` → bool
Writes content to a file, creating it if it doesn't exist or truncating if it does.
```lux
if (writeFile("output.txt", "Hello, World!")) {
    print "File written successfully";
}
```

#### `appendFile(path, content)` → bool
Appends content to the end of an existing file.
```lux
if (appendFile("log.txt", "New log entry\n")) {
    print "Log updated";
}
```

#### `deleteFile(path)` → bool
Deletes a file from the filesystem.
```lux
if (deleteFile("temp.txt")) {
    print "File deleted";
}
```

#### `fileExists(path)` → bool
Checks if a file exists.
```lux
if (fileExists("config.txt")) {
    var config = readFile("config.txt");
}
```

### Directory Operations

#### `createDir(path)` → bool
Creates a new directory.
```lux
if (createDir("data")) {
    print "Directory created";
}
```

#### `listDir(path)` → string or nil
Lists directory contents as a newline-separated string.
```lux
var files = listDir(".");
if (files != nil) {
    print "Files in current directory:";
    print files;
}
```

### Time Function

#### `clock()` → number
Returns the current time in seconds (useful for benchmarking).
```lux
var start = clock();
// ... some code ...
var elapsed = clock() - start;
print "Elapsed: " + elapsed + " seconds";
```

### Math

`floor` is the integer floor. The rest wrap `math.h` (IEEE NaN/Inf on domain errors; bad arity or type returns `nil`).

```lux
print abs(-3);      // 3
print ceil(1.2);    // 2
print sqrt(9);      // 3
print pow(2, 10);   // 1024
print log(1);       // 0
print sin(0);       // 0
print cos(0);       // 1
```

### JSON Operations

Lux includes native JSON parsing and serialization for easy data interchange.

#### `parseJSON(jsonString)` → value or nil
Parses a JSON string and returns a Lux value. JSON objects become Lux class instances with properties. JSON arrays become native Lux arrays (index with `arr[0]`, size with `arr.length`).

```lux
var jsonStr = "{\"name\":\"Alice\",\"age\":30,\"active\":true}";
var obj = parseJSON(jsonStr);
if (obj != nil) {
    print obj.name;   // Alice
    print obj.age;    // 30
    print obj.active; // true
}

// Array example
var arrStr = "[1,2,3,4,5]";
var arr = parseJSON(arrStr);
if (arr != nil) {
    print arr.length; // 5
    print arr[0];     // 1
    print arr[4];     // 5
}

// Nested objects
var nested = "{\"user\":{\"name\":\"Bob\",\"level\":5}}";
var data = parseJSON(nested);
print data.user.name;  // Bob
```

#### `toJSON(value)` → string or nil
Converts a Lux value to a JSON string. Supports numbers, strings, booleans, nil, and objects (class instances).

**JSON in Lux scripts — escaping tip:**
- Inside a Lux string literal, every JSON `"` must be written as `\"`:
  `parseJSON("{\"name\":\"Alice\"}")` or `httpPost(url, "{\"ok\":true}")`
- Prefer building objects (classes) and calling `toJSON(...)` so you never hand-escape quotes.
- Do **not** call `toJSON` on a string that is already JSON text — that double-encodes it into a JSON string value (`"{\"ok\":true}"` instead of `{"ok":true}`).

```lux
class Person {
    init(name, age) {
        this.name = name;
        this.age = age;
    }
}

var person = Person("Charlie", 25);
var json = toJSON(person);
print json;  // {"name":"Charlie","age":25}

// Round-trip: parse and serialize
var original = "{\"x\":10,\"y\":20}";
var parsed = parseJSON(original);
var serialized = toJSON(parsed);
print serialized;  // {"x":10,"y":20}
```

**Practical Example: Save/Load Game Data**
```lux
// Save game state
class GameState {
    init(level, score) {
        this.level = level;
        this.score = score;
    }
}

var state = GameState(5, 1000);
var json = toJSON(state);
writeFile("save.json", json);

// Load game state
var loaded = readFile("save.json");
if (loaded != nil) {
    var state = parseJSON(loaded);
    print "Level: " + state.level;
    print "Score: " + state.score;
}
```

### String Operations

Lux includes native string parsing primitives for building custom parsers and text manipulation.

#### String Indexing
Strings can be indexed like arrays for read access. Indexing returns a single-character string.

```lux
var s = "hello";
print s[0];  // "h"
print s[1];  // "e"

var i = 0;
while (i < len(s)) {
    print s[i];
    i = i + 1;
}
```

String assignment by index is not supported (`s[0] = "H"` is invalid).

#### `len(string)` → number
Returns the length of a string in bytes.

```lux
var text = "Hello, World!";
print len(text);  // 13
```

#### `strFind(haystack, needle, [start])` → number
Searches for `needle` in `haystack` starting at optional `start` index. Returns the index of the first match, or `-1` if not found.

```lux
var text = "alpha,beta,gamma";

print strFind(text, "beta");        // 6
print strFind(text, "beta", 0);     // 6
print strFind(text, "beta", 7);     // -1 (not found after index 7)
print strFind(text, "nothere");     // -1
print strFind(text, ",");           // 5 (first comma)
```

#### `strSlice(text, start, end)` → string
Extracts a substring from `start` (inclusive) to `end` (exclusive). Negative or out-of-bounds indices are clamped to valid ranges.

```lux
var text = "Hello, World!";

print strSlice(text, 0, 5);   // "Hello"
print strSlice(text, 7, 12);  // "World"
print strSlice(text, 7, 100); // "World!" (clamped to end)

// Extract between delimiters
var data = "key=value";
var eqIdx = strFind(data, "=");
if (eqIdx != -1) {
    var key = strSlice(data, 0, eqIdx);
    var value = strSlice(data, eqIdx + 1, len(data));
    print "Key: " + key;      // "key"
    print "Value: " + value;  // "value"
}
```

#### `strStartsWithAt(text, pattern, index)` → bool
Checks if `pattern` starts at position `index` in `text`. Useful for efficient pattern matching without creating substrings.

```lux
var text = "alpha,beta,gamma";

print strStartsWithAt(text, "alpha", 0);   // true
print strStartsWithAt(text, "beta", 6);    // true
print strStartsWithAt(text, "beta", 7);    // false
print strStartsWithAt(text, ",", 5);       // true
```

#### `strTrim(text)` → string
Removes leading and trailing ASCII whitespace (space, tab, newline, carriage return).

```lux
var text = "  Hello, World!  \n";
print strTrim(text);  // "Hello, World!"

// Parse user input
var input = "  alice@example.com\t";
var email = strTrim(input);
print email;  // "alice@example.com"
```

#### `strSplit(text, separator)` → array
Splits `text` into an array of strings using `separator` as the delimiter. If separator is empty, splits into individual characters.

```lux
var csv = "alpha,beta,gamma";
var parts = strSplit(csv, ",");
print parts.length;  // 3
print parts[0];      // "alpha"
print parts[1];      // "beta"
print parts[2];      // "gamma"

// Split into characters
var word = "abc";
var chars = strSplit(word, "");
print chars.length;  // 3
print chars[0];      // "a"
print chars[1];      // "b"

// Parse lines
var multiline = "line1\nline2\nline3";
var lines = strSplit(multiline, "\n");
var i = 0;
while (i < lines.length) {
    print "Line " + (i + 1) + ": " + lines[i];
    i = i + 1;
}
```

**Practical Example: Custom XML Parser**
```lux
// Extract all values for a given XML tag
fun extractXmlTag(xml, tagName) {
    var results = [];
    var openTag = "<" + tagName + ">";
    var closeTag = "</" + tagName + ">";
    var cursor = 0;
    
    while (cursor < len(xml)) {
        var openIdx = strFind(xml, openTag, cursor);
        if (openIdx == -1) {
            return results;
        }
        
        var valueStart = openIdx + len(openTag);
        var closeIdx = strFind(xml, closeTag, valueStart);
        if (closeIdx == -1) {
            return results;
        }
        
        var value = strSlice(xml, valueStart, closeIdx);
        results = results + [strTrim(value)];
        cursor = closeIdx + len(closeTag);
    }
    
    return results;
}

var xmlResponse = "<items><item>A</item><item>B</item><item>C</item></items>";
var items = extractXmlTag(xmlResponse, "item");
print items.length;  // 3
print items[0];      // "A"
print items[1];      // "B"
```


### Array Algorithms

Lux includes native array search and sort helpers.

#### `arrayIndexOf(array, value)` -> number
Returns the index of the first matching element, or `-1` if not found.

```lux
var nums = [10, 20, 30, 20];
print arrayIndexOf(nums, 20);  // 1
print arrayIndexOf(nums, 99);  // -1
```

#### `arrayContains(array, value)` -> bool
Returns `true` if the array contains the value.

```lux
var names = ["alice", "bob", "carol"];
print arrayContains(names, "bob");   // true
print arrayContains(names, "dave");  // false
```

#### `arraySort(array)` -> array
Sorts an array in place and returns the same array. Supports all-number arrays or all-string arrays.

```lux
var nums = [9, 2, 7, 1, 5];
arraySort(nums);
print nums;  // [1, 2, 5, 7, 9]

var words = ["pear", "apple", "orange", "banana"];
arraySort(words);
print words; // ["apple", "banana", "orange", "pear"]

var mixed = [1, "two", 3];
print arraySort(mixed); // nil (mixed types not sortable)
```

#### `arrayBinarySearch(array, value)` -> number
Performs binary search on a sorted array. Returns the index if found, or `-1`.

```lux
var nums = [9, 2, 7, 1, 5];
arraySort(nums);  // required before binary search

print arrayBinarySearch(nums, 7);   // 3
print arrayBinarySearch(nums, 42);  // -1

var words = ["pear", "apple", "orange", "banana"];
arraySort(words);
print arrayBinarySearch(words, "banana"); // 1
```

### Float64Array

**Platform**: Plan 9 and POSIX (macOS, Linux, OpenBSD).

Float64Array provides a typed double-precision buffer for numerical work. Use it for dot products, moving averages, or when you need faster element access than regular Lux arrays.

#### `float64_available()` → bool

Returns `true` if Float64Array natives are available in this build. Use this to skip numeric tests on older binaries that lack the natives.

```lux
if (float64_available()) {
    var a = float64_new(100);
    // ...
}
```

#### `float64_new(length)` → Float64Array

Creates a new Float64Array of the given length, initialized to zeros.

```lux
var a = float64_new(100);
```

#### `float64_get(array, index)` → number

Returns the element at `index`. Indices are 0-based. Fails if index is out of bounds.

```lux
var val = float64_get(a, 5);
```

#### `float64_set(array, index, value)` → bool

Sets the element at `index` to `value`. Returns `true` on success. Fails if index is out of bounds.

```lux
float64_set(a, 0, 1.5);
float64_set(a, i, (i % 100) * 0.01);
```

#### `float64_dot(a, b)` → number

Computes the dot product of two Float64Arrays. Both arrays must have the same length. Uses a native C implementation for speed.

```lux
var a = float64_new(1000);
var b = float64_new(1000);
// ... fill a and b ...
var sum = float64_dot(a, b);
```

#### `float64_fill(array, value)` → Float64Array
Sets every element to `value` and returns the same array.

#### `float64_copy(array)` → Float64Array
Returns a new Float64Array with the same elements.

#### `float64_slice(array, start, end)` → Float64Array
Copies `[start, end)` (clamped to bounds), same half-open rule as `strSlice`.

#### `float64_sum(array)` / `float64_mean(array)` / `float64_min(array)` / `float64_max(array)` → number or nil
Reductions. `mean` / `min` / `max` return `nil` on an empty array; `sum` of empty is `0`.

#### `float64_add(a, b)` / `float64_mul(a, b)` → Float64Array
Elementwise sum or product. Same length required. Returns a **new** array; `a` and `b` are unchanged.

#### `float64_axpy(a, x, y)` → Float64Array
BLAS-style `y[i] += a * x[i]` in place. Same length required. Returns `y`.

```lux
var x = float64_new(3);
var y = float64_new(3);
float64_fill(x, 2);
float64_fill(y, 1);
float64_axpy(0.5, x, y);  // y is now [2, 2, 2]
```

**Example — moving average:**

```lux
var data = float64_new(10000);
var kernel = float64_new(64);
// Fill kernel with 1/64 for uniform average
var i = 0;
while (i < 64) {
    float64_set(kernel, i, 1.0 / 64);
    i = i + 1;
}
// Compute dot product of sliding window with kernel
var view = float64_new(64);
var idx = 0;
while (idx + 64 <= 10000) {
    var k = 0;
    while (k < 64) {
        float64_set(view, k, float64_get(data, idx + k));
        k = k + 1;
    }
    var avg = float64_dot(view, kernel);
    idx = idx + 1;
}
```

**Tip**: Reuse a single `view` buffer in loops instead of allocating a new one each iteration to avoid GC pressure.

### DataFrame, CSV, and graphs

Lux is not a pandas/numpy replacement. For moderate tables and small graphs, import modules from `lib/` (paths are relative to the process working directory; tests run from `posix/` use `../lib/...`). Multipart HTTP lives in `lib/http.lux` (`Form`). OpenAPI / Swagger UI lives in `lib/swagger.lux` (`Op`, `swagger`). OAuth 2.0 client-credentials lives in `lib/oauth.lux` (`OAuth`).

#### CSV and DataFrame (`lib/dataframe.lux`)

Quoted CSV via native `parseCSV(text, [sep])` (Lux `parseCSVQuoted` is the fallback). `DataFrame` is column-oriented.

```lux
import "../lib/dataframe.lux";

var df = read_csv_text("name,age\nAlice,30\nBob,25\nCarol,27");
df.toNumber("age");
print df.sortBy("age").to_csv();

var gb = df.groupBy("name", "age", "mean");
print gb.to_csv();

var rec = fromRecords([["Ada", 1]], ["name", "id"]);
// SQL: fromRecords(dbQuery(conn, "SELECT name, id FROM t"), ["name", "id"])
```

Useful methods: `head`, `select`, `filter` (predicate gets a row object), `mapColumn`, `toNumber`, `sortBy`, `groupBy(key, value, agg)` with `count`/`sum`/`mean`/`min`/`max` (`groupBy(key, nil, nil)` counts), `unique`, `valueCounts`, `describe`, `to_csv` (RFC 4180 quoting), `read_csv_file`. `getField(obj, name)` looks up an instance field by string (for `dbQuery` rows). `parseCSV(text, sep)` accepts an optional separator; `read_csv_text` always uses comma.

#### Graphs (`lib/graph.lux`)

Edge-list `Graph` (set `g.directed = true` for directed). `addNode`, `addEdge`, `neighbors`, `bfs`, `dfs`, `toDot()` for Graphviz.

```lux
import "../lib/graph.lux";

var g = Graph();
g.addEdge("alice", "bob");
g.addEdge("bob", "carol");
writeFile("friends.dot", g.toDot());
```

See `examples/dataframe_prototype.lux` and `examples/graph_demo.lux`.

#### Web tests (`lib/webtest.lux`, POSIX)

Library-only wrappers around `curl`, `k6`, and Playwright. They write a temp script, `run()` it, and parse files — no VM changes. Plan 9 can still generate scripts; live `.run()` needs those binaries.

```lux
import "../lib/webtest.lux";

var r = HttpCheck().get("https://example.com/health");
assert(r.status == 200, "health status");
assert(strFind(r.body, "ok") >= 0, "health body");

var k = K6();
k.get("https://example.com/api/items");
k.vus(10);
k.duration("30s");
var s = k.run();   // needs k6 on PATH
assert(s.failed < 0.01, "error rate");
assert(s.p95 < 500, "p95");

var p = Playwright();
p.goto("https://example.com/login");
p.fill("#user", "admin");
p.click("button[type=submit]");
p.text("h1");
var out = p.run();   // needs node + npx playwright
assert(out.ok == true, "playwright");
```

`HttpCheck` uses curl (`-K` config) so you get `status` / `headers` / `body` (`ok` is 2xx). Headers are a `Dict` or an array of `"Name: value"` strings. `K6.parseSummary` maps `p(95)` via `getField`. Playwright queues `goto` / `fill` / `click` / `waitFor` / `text` / `screenshot` then launches one browser. See `examples/webtest_demo.lux`.

### HTTP Operations

Lux includes HTTP client functions for making web requests on both Plan 9 and POSIX platforms.

**Platform Support:**
- **Plan 9**: HTTP and HTTPS (uses native `dial()` with libsec TLS)
- **POSIX**: HTTP and HTTPS (uses libcurl)

#### `httpGet(url)` → string or nil
Makes an HTTP GET request and returns the response body as a string (not a response object — there is no `statusCode` or `.body` field).

```lux
// HTTPS example (both platforms) — single product object
var response = httpGet("https://fakestoreapi.com/products/1");
if (response != nil) {
    print "Received: " + response;

    var data = parseJSON(response);
    if (data != nil) {
        print "Title: " + data.title;
        print "Price: " + data.price;
    }
} else {
    print "Request failed";
}

// HTTP example (both Plan 9 and POSIX)
var httpResp = httpGet("http://httpbin.org/get");
if (httpResp != nil) {
    var data = parseJSON(httpResp);
    print "URL: " + data.url;
}
```

#### `httpPost(url, body)` → string or nil
Makes an HTTP POST request with JSON body and returns the response. Always sends `Content-Type: application/json`. For `multipart/form-data`, use `httpRequest` with a `Form` or `httpPostForm`.

```lux
class User {
    init(name, email) {
        this.name = name;
        this.email = email;
    }
}

var user = User("Alice", "alice@example.com");
var jsonBody = toJSON(user);

if (jsonBody != nil) {
    var response = httpPost("https://api.example.com/users", jsonBody);
    if (response != nil) {
        print "User created!";
        var result = parseJSON(response);
        if (result != nil) {
            print "ID: " + result.id;
        }
    }
}
```

#### `httpPostForm(url, parts, [headers])` → string or nil
POST `multipart/form-data`. `parts` is a `Form` instance or an array of part instances (`name`, optional `value` / `path` / `filename` / `type`). File parts are read from disk in C (length-aware). Extra headers are merged; `Content-Type` is always set with a boundary.

Prefer the `Form` helper in `lib/http.lux`:

```lux
import "../lib/http.lux";

class Headers { init() {} }

var form = Form();
form.field("method", "GET");
form.field("name", "legacy");
form.file("file", "script.js", "application/x-javascript");

var headers = Headers();
headers.accept = "application/json";

var resp = httpRequest("POST", "https://api.example.com/scenarios", form, headers);
// or: var resp = form.post(url, headers);
```

#### `httpPut(url, body)` → string or nil
Makes an HTTP PUT request with JSON body and returns the response.

```lux
class UpdateData {
    init(status) {
        this.status = status;
    }
}

var update = UpdateData("active");
var jsonBody = toJSON(update);

if (jsonBody != nil) {
    var response = httpPut("https://api.example.com/resource/123", jsonBody);
    if (response != nil) {
        print "Resource updated!";
    }
}
```

#### `httpServer(port)` → bool
Starts a basic HTTP server listening on the specified port. The server runs in a blocking loop and responds to requests with built-in routes. Press Ctrl+C to stop the server.

**Built-in Routes:**
- `GET /` → Returns a hello message
- `GET/POST /echo` → Echoes back the request method, path, and body
- Other paths → Returns 404 Not Found

```lux
// Start a server on port 8080
print "Starting HTTP server on port 8080...";
httpServer(8080);
```

**Testing from command line:**
```sh
# From another terminal or machine:
curl http://localhost:8080/
curl http://localhost:8080/echo
curl -X POST http://localhost:8080/echo -d '{"test":"data"}'
```

**Features:**
- Listens on all network interfaces (accessible from external machines)
- Automatic Content-Length header parsing for POST bodies
- Returns JSON responses with proper HTTP headers
- Handles multiple sequential connections
- Simple built-in routing

**Use Cases:**
- Quick HTTP API testing
- Local web service development
- Receiving webhooks
- Simple microservices
- Development/debugging endpoints

**Platform Support:**
- **Plan 9**: Uses native `announce()`, `listen()`, `accept()` system calls
- **POSIX**: Uses standard BSD sockets API (`socket()`, `bind()`, `listen()`, `accept()`)

#### `Server` class — routing, static files, virtual hosts, middleware (Plan 9 and POSIX)

Create a configurable HTTP server with user-defined routes. See `examples/static_server.lux`, `examples/template_server/server.lux`, `examples/vhost_server.lux`, and `tests/test_http_server_routes.lux`.

```lux
fun handleHello(req, res) { res.send("Hello from Lux!"); }
/* res.send → text/plain; res.html → text/html; res.json → application/json */

fun handleData(req, res) {
  var body = parseJSON(req.body);
  if (body == nil) {
    res.status(400);
    res.send("{\"error\":\"invalid json\"}");
    return;
  }
  print body;
  res.status(201);
  res.json(body);   /* echo parsed JSON, e.g. {"name":"John"} */
}

/* Fixed JSON response (not a Lux object literal — use parseJSON): */
fun handleAck(req, res) {
  res.status(201);
  res.json(parseJSON("{\"status\":\"received\"}"));
}

var server = Server(8080);
server.workers(4);   /* prefork: N processes accept in parallel (default 4; 1 = no fork) */
server.get("/hello", handleHello);
server.post("/data", handleData);
server.static("public");   /* serve GET files from ./public/ */
server.start();
```

**Concurrency (`server.workers(n)`):** Prefork model — after bind/announce, Lux spawns `n` worker processes (clamped to 1..32; default **4** if unset). Each worker has its own VM copy and runs an accept/handle loop; the parent waits and respawns dead workers. `workers(1)` keeps a single-process loop (handy for debugging). In-memory mutations are **not** shared across workers after fork; use files, DB, or external services for shared state. TLS still terminates outside Lux (`tlssrv` on 9front).

**Virtual hosts (many domains, one port):** Lux parses the `Host` header into `req.host` (port stripped). Use `server.vhost(host, root)` for per-domain static trees and `server.getHost` / `server.postHost` for host-scoped routes. Global `get`/`post` still match any Host. Host matching is case-insensitive. GET static files are tried first (vhost root, else `server.static`), then host-specific routes, then global routes.

```lux
fun helloTech(req, res) {
  res.json(parseJSON("{\"site\":\"technomancy\"}"));
}
fun helloWill(req, res) {
  print req.host;
  res.json(parseJSON("{\"site\":\"william\"}"));
}
fun health(req, res) {
  res.json(parseJSON("{\"ok\":true}"));
}

var server = Server(8080);
server.vhost("technomancy.site", "/usr/www/sites/technomancy.site");
server.vhost("williamgunnells.com", "/usr/www/sites/williamgunnells.com");
server.getHost("technomancy.site", "/api/hello", helloTech);
server.getHost("williamgunnells.com", "/api/hello", helloWill);
server.get("/health", health);   /* any Host */
server.start();
```

```sh
curl -H 'Host: technomancy.site' http://127.0.0.1:8080/
curl -H 'Host: williamgunnells.com' http://127.0.0.1:8080/api/hello
```

**9front edge (TLS / www redirects stay outside Lux):** run Lux on `:8080` behind `tlssrv` (SNI/acmed certificates). Keep `rc-httpd` for ACME http-01 on port 80 and `www.*` → apex redirects; register only apex names with `vhost`/`getHost`. See also [TODO.md](TODO.md).

**`res.json` notes:**
- Pass a **value** to serialize (`parseJSON` objects, class instances with fields), not pre-built JSON text.
- `res.json("{\"a\":1}")` returns a JSON **string** (`"{\"a\":1}"`), not an object.
- `Dict` data lives in native storage; `res.json(aDict)` only sees internal fields (e.g. `_ptr`). Use `parseJSON`/`toJSON`, `dict.iter()`, or `res.send` with a string you build. See `tests/test_dict.lux`.

**AWS Lambda (Mangum-style):** Use the same `Server` routes without `start()`. `server.handle(method, path, [body], [host], [authorization])` runs one request and returns `{statusCode, body, contentType}`. `lib/mangum.lux` maps API Gateway REST (v1), HTTP API / Function URL (v2) events onto that (including `Authorization`), and writes a Lambda proxy response. The custom runtime in `lambda/bootstrap` still feeds `/tmp/lambda_event.json` and posts `/tmp/lambda_response.json`.

```lux
import "../lib/mangum.lux";   /* or "lib/mangum.lux" when cwd is the repo / Lambda task root */

fun handleHome(req, res) {
  res.html("<h1>Hello from Lux</h1>");
}

var server = Server(0);   /* port unused for handle / Mangum */
server.get("/", handleHome);

/* Local, no socket: */
var page = server.handle("GET", "/");
print page.statusCode;    /* 200 */
print page.body;

/* On Lambda (see lambda/handler.lux): */
Mangum(server);
```

Package `lux`, `lambda/bootstrap`, `lambda/handler.lux`, and `lib/mangum.lux` so the task cwd is `/var/task`.

Same routes in both examples (`GET /` HTML, `GET /health` JSON, `GET`/`POST /echo`):

- `examples/lambda_web.lux` — local `server.start()` on 8084; `--once` uses `server.handle`; `Mangum` when `/tmp/lambda_event.json` exists
- `lambda/handler.lux` — Lambda custom-runtime entry (`Mangum(server)`)

See `tests/test_mangum.lux`.

**Middleware (`server.use`):** Register functions with signature `(req, res, next)`. Call `next()` to continue the chain (more middleware, then the matched route handler). See `examples/server_middleware.lux`.

```lux
fun logger(req, res, next) {
  print "Request: " + req.method + " " + req.path;
  next();
}
server.use(logger);
```

Omit `next()` to short-circuit — send a response yourself and the route handler never runs:

```lux
fun requireAuth(req, res, next) {
  if (req.path == "/secret") {
    res.status(401);
    res.send("unauthorized");
    return;   /* no next() → handler never runs */
  }
  next();
}
server.use(requireAuth);
```

**OpenAPI / Swagger UI (`lib/swagger.lux`):** `swagger(server)` mounts FastAPI-style `GET /docs` (Swagger UI) and `GET /openapi.json` from the route table. Pass an optional `Op` as the last argument to `get` / `post` / `getHost` / `postHost`, or attach one later with `swaggerDoc`. See `examples/swagger_server.lux` and `tests/test_swagger.lux`.

```lux
import "../lib/swagger.lux";

fun handleHealth(req, res) {
  res.json(parseJSON("{\"ok\":true}"));
}

fun handleEcho(req, res) {
  res.json(parseJSON("{\"ok\":true}"));
}

var server = Server(8085);
server.swaggerTitle = "Demo API";   /* optional; default "Lux API" */

var healthOp = Op("Health check", "ops");
healthOp.example = parseJSON("{\"ok\":true}");
server.get("/health", handleHealth, healthOp);

var echoOp = Op("Echo the request", "echo");
echoOp.body = parseJSON("{\"msg\":\"hi\"}");
echoOp.status = 200;
server.post("/echo", handleEcho, echoOp);

/* After the fact, e.g. from an imported route file: */
server.get("/later", handleEcho);
swaggerDoc(server, "GET", "/later", Op("Registered later", "misc"));

swagger(server);
server.start();
```

`Op(summary, tag)` also accepts `description`, `body` (example request), `example` (example response), and `status` (response code, default 200). Without an `Op`, the spec still lists the path so Try it out works. `/docs` and `/openapi.json` are omitted from the spec. `server.swaggerVersion` defaults to `"1.0.0"`. Swagger UI loads from a CDN; `GET /openapi.json` works offline.

**OAuth 2.0 (`lib/oauth.lux`):** Client-credentials grant that **issues a Bearer token**. `oa.mount(server)` adds `POST /oauth/token` and checks `Authorization: Bearer …` on other routes. Token requests and paths in `oa.skip` (default `/oauth/token`, `/docs`, `/openapi.json`) stay public. A valid token sets `req.clientId`. See `examples/oauth_server.lux` and `tests/test_oauth.lux`.

```lux
import "../lib/oauth.lux";
import "../lib/swagger.lux";

fun handleHealth(req, res) {
    res.json(parseJSON("{\"ok\":true}"));
}
fun handleSecret(req, res) {
    var obj = parseJSON("{\"ok\":true,\"client\":\"\"}");
    obj.client = req.clientId;
    res.json(obj);
}

var server = Server(8086);
var oa = OAuth("change-me");
oa.addClient("demo", "demo-secret");
oa.skip[oa.skip.length] = "/health";
oa.mount(server);

server.get("/health", handleHealth);
server.get("/secret", handleSecret);
swagger(server);   /* /docs Authorize: paste Bearer token, or client credentials */
server.start();
```

Mint a token, then send it as Bearer:

```lux
var g = oa.grant("demo", "demo-secret");
print g.token_type;        /* Bearer */
print g.access_token;

var headers = parseJSON("{}");
headers.Authorization = oa.bearer("demo", "demo-secret");   /* "Bearer <token>" */
var resp = httpRequest("GET", "http://127.0.0.1:8086/secret", nil, headers);
```

```sh
curl -s -X POST http://127.0.0.1:8086/oauth/token \
  -d '{"grant_type":"client_credentials","client_id":"demo","client_secret":"demo-secret"}'
# {"access_token":"demo.<exp>.<hmac>","token_type":"Bearer","expires_in":3600}

curl -s -H 'Authorization: Bearer <access_token>' http://127.0.0.1:8086/secret
```

JSON or `application/x-www-form-urlencoded` bodies work on the token endpoint. Tokens are `clientId.exp.hmac` signed with `oa.secret`; `oa.expiresIn` defaults to 3600 seconds. `client_id` must not contain `.`. This is machine-to-machine auth, not browser login (no authorization-code redirect). If `oa.mount` runs before `swagger()`, `/docs` advertises HTTP Bearer plus the token URL.

**Test from another machine:**
```sh
curl http://10.0.0.31:8080/hello
curl -H "Content-Type: application/json" -d '{"name":"John"}' http://10.0.0.31:8080/data
```

**Controller imports:** Define `server` before importing route modules so they can register routes:
```lux
var server = Server(8080);
import "routes/user.lux";   /* routes/user.lux calls server.get(...) */
server.start();
```

**Simple persistence:** Use globals holding `parseJSON` results (not `{}` map literals):
```lux
var lastPost = nil;

fun postData(req, res) {
  lastPost = parseJSON(req.body);
  res.status(201);
  res.json(parseJSON("{\"ok\":true}"));
}

fun getData(req, res) {
  if (lastPost == nil) {
    res.json(parseJSON("{\"data\":null}"));
  } else {
    res.json(lastPost);
  }
}

var server = Server(8080);
server.post("/data", postData);
server.get("/data", getData);
server.start();
```

**Practical Example: Fetch and Process API Data**
```lux
// Fetch user data from API
var response = httpGet("https://jsonplaceholder.typicode.com/users/1");
if (response != nil) {
    var user = parseJSON(response);
    if (user != nil) {
        print "Name: " + user.name;
        print "Email: " + user.email;
        print "City: " + user.address.city;
        
        // Save locally
        writeFile("user_" + user.id + ".json", response);
    }
}

// Post analysis results
class AnalysisResult {
    init(userId, score, status) {
        this.userId = userId;
        this.score = score;
        this.status = status;
    }
}

var result = AnalysisResult(1, 95, "passed");
var json = toJSON(result);
if (json != nil) {
    var postResp = httpPost("https://api.example.com/results", json);
    if (postResp != nil) {
        print "Results submitted successfully";
    }
}
```

**Implementation Notes:**
- **Plan 9 HTTP Client**: Uses native `dial()` system call with manual HTTP/1.0 protocol and `libsec` for TLS/HTTPS support. HTTP (port 80) and HTTPS (port 443) both supported with automatic fallback.
- **POSIX HTTP Client**: Uses libcurl library with full HTTP/1.1 and HTTPS support, automatic redirects, and robust error handling.
- **HTTP Client (Both)**: 30-second timeouts (POSIX), automatic `Content-Type: application/json` header, returns `nil` on failure.
- **Plan 9 HTTP Server**: Uses native `announce()`, `listen()`, `accept()` system calls with manual HTTP/1.0 protocol parsing.
- **POSIX HTTP Server**: Uses standard BSD sockets (`socket()`, `bind()`, `listen()`, `accept()`) with HTTP/1.0 protocol.
- **HTTP Server (Both)**: Binds to all interfaces, handles Content-Length parsing, supports sequential connections, returns JSON responses.

### Network

DNS lookup and a TCP-connect probe. These do **not** shell out via `run()` (`dig` / `ping`). `netPing` is not ICMP echo; it times a TCP `connect` to a port (same idea as `tcping`).

**Platform Support:**
- **Plan 9**: `netLookup` queries `/net/cs`; `netPing` uses `dial("tcp!host!port")` timed with `nsec()`
- **POSIX**: `netLookup` uses `getaddrinfo`; `netPing` uses a non-blocking `connect` with a 2-second timeout

#### `netLookup(host)` → string or nil
Resolve a hostname to its first IP address. Returns `nil` on failure. The first address may be IPv4 or IPv6, depending on resolver order (`localhost` is often `::1`).

```lux
var ip = netLookup("example.com");
if (ip != nil) {
    print "resolved: " + ip;
}
```

#### `netPing(host, port)` → number
TCP-connect probe. Returns round-trip milliseconds on success, or `-1` if the host is unreachable, the port is closed/filtered, the timeout expires, or the arguments are invalid. `port` is required (use `80` or `443`). On Plan 9 the time includes name resolution.

```lux
var ms = netPing("example.com", 443);
if (ms >= 0) {
    print "reachable, rtt " + ms + " ms";
} else {
    print "down or filtered";
}
```

### AWS S3 Operations

Lux includes native AWS S3 integration with AWS Signature Version 4 authentication. This provides boto3-like functionality for direct S3 API access using AWS credentials.

**Authentication Support:**
- ✅ Permanent credentials (AWS Access Key + Secret Key)
- ✅ Temporary credentials (STS Session Token)
- ✅ AWS Signature Version 4 compliant
- ✅ All AWS regions supported

**Platform Support:**
- **Plan 9**: Uses libsec (HMAC-SHA256) with native TLS
- **POSIX**: Uses OpenSSL (HMAC-SHA256) with libcurl

#### `s3ListObjects(bucket, accessKey, secretKey, region, [prefix], [sessionToken])` → string or nil

Lists objects in an S3 bucket. Returns the XML response from S3's ListObjectsV2 API.

**Parameters:**
- `bucket`: S3 bucket name
- `accessKey`: AWS access key ID
- `secretKey`: AWS secret access key
- `region`: AWS region (e.g., "us-east-1", "eu-west-1")
- `prefix` (optional): Filter objects by prefix
- `sessionToken` (optional): STS session token for temporary credentials

```lux
// List all objects in bucket
var bucket = "my-bucket"
var accessKey = "AKIAIOSFODNN7EXAMPLE"
var secretKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
var region = "us-east-1"

var response = s3ListObjects(bucket, accessKey, secretKey, region)
if (response != nil) {
    print "Objects in bucket:"
    print response
}

// List objects with prefix
var logs = s3ListObjects(bucket, accessKey, secretKey, region, "logs/")
if (logs != nil) {
    print "Log files:"
    print logs
}

// Using STS temporary credentials
var sessionToken = "FwoGZXIvYXdzEBYaDJvZ3..."  // From STS
var result = s3ListObjects(bucket, accessKey, secretKey, region, "", sessionToken)
if (result != nil) {
    print "Listed with temporary credentials"
}
```

#### `s3GetObject(bucket, key, accessKey, secretKey, region, [sessionToken])` → string or nil

Downloads an object from S3 and returns its contents as a string.

**Parameters:**
- `bucket`: S3 bucket name
- `key`: Object key (path in bucket)
- `accessKey`: AWS access key ID
- `secretKey`: AWS secret access key
- `region`: AWS region
- `sessionToken` (optional): STS session token

```lux
// Download a file
var content = s3GetObject(bucket, "data/file.txt", accessKey, secretKey, region)
if (content != nil) {
    print "File contents:"
    print content
    
    // Save locally
    writeFile("downloaded.txt", content)
}

// Download JSON configuration
var configKey = "config/app.json"
var configStr = s3GetObject(bucket, configKey, accessKey, secretKey, region)
if (configStr != nil) {
    var config = parseJSON(configStr)
    if (config != nil) {
        print "App version: " + config.version
        print "Environment: " + config.environment
    }
}

// Using temporary credentials
var fileData = s3GetObject(bucket, "reports/report.csv", 
                           accessKey, secretKey, region, sessionToken)
if (fileData != nil) {
    print "Downloaded report with STS token"
}
```

#### `s3PutObject(bucket, key, content, accessKey, secretKey, region, [sessionToken])` → bool

Uploads content to S3. Returns `true` on success, `false` on failure.

**Parameters:**
- `bucket`: S3 bucket name
- `key`: Object key (destination path)
- `content`: Content to upload (as string)
- `accessKey`: AWS access key ID
- `secretKey`: AWS secret access key
- `region`: AWS region
- `sessionToken` (optional): STS session token

```lux
// Upload a text file
var content = "Hello from Lux! Timestamp: " + clock()
var success = s3PutObject(bucket, "test/hello.txt", content, 
                          accessKey, secretKey, region)
if (success) {
    print "File uploaded successfully"
} else {
    print "Upload failed"
}

// Upload JSON data
class DataPoint {
    init(timestamp, value, status) {
        this.timestamp = timestamp
        this.value = value
        this.status = status
    }
}

var data = DataPoint(clock(), 42, "active")
var jsonStr = toJSON(data)
if (jsonStr != nil) {
    var uploaded = s3PutObject(bucket, "data/point.json", jsonStr,
                               accessKey, secretKey, region)
    if (uploaded) {
        print "JSON data uploaded to S3"
    }
}

// Upload with temporary credentials
var logData = "Application log: System started\n"
var result = s3PutObject(bucket, "logs/app.log", logData,
                         accessKey, secretKey, region, sessionToken)
if (result) {
    print "Log uploaded with STS credentials"
}
```

**Practical Example: S3 Data Pipeline**
```lux
// Configuration
var bucket = "analytics-bucket"
var accessKey = "YOUR_ACCESS_KEY"
var secretKey = "YOUR_SECRET_KEY"
var region = "us-west-2"

// 1. Download raw data from S3
print "Downloading raw data..."
var rawData = s3GetObject(bucket, "input/data.json", accessKey, secretKey, region)
if (rawData == nil) {
    print "Error: Could not download data"
} else {
    // 2. Process the data
    var data = parseJSON(rawData)
    if (data != nil) {
        print "Processing " + data.count + " records"
        
        // 3. Generate report
        class Report {
            init(timestamp, recordCount, status) {
                this.timestamp = timestamp
                this.recordCount = recordCount
                this.status = status
            }
        }
        
        var report = Report(clock(), data.count, "completed")
        var reportJson = toJSON(report)
        
        // 4. Upload results back to S3
        if (reportJson != nil) {
            var uploaded = s3PutObject(bucket, "output/report.json", reportJson,
                                      accessKey, secretKey, region)
            if (uploaded) {
                print "Report uploaded successfully"
            } else {
                print "Error: Upload failed"
            }
        }
    }
}

// 5. List all processed files
print "Listing processed files..."
var files = s3ListObjects(bucket, accessKey, secretKey, region, "output/")
if (files != nil) {
    print "Output files:"
    print files
}
```

**Use Cases:**
- Data pipeline processing (download, transform, upload)
- Configuration management (store/retrieve app configs)
- Log aggregation and analysis
- Backup and restore operations
- File synchronization
- Static site deployment
- Serverless data processing

**Security Best Practices:**
- ✅ Use STS temporary credentials when possible
- ✅ Store credentials in environment variables or config files (not in code)
- ✅ Use IAM roles with minimum required permissions
- ✅ Rotate access keys regularly
- ✅ Enable S3 bucket encryption
- ✅ Use VPC endpoints for private S3 access
- ✅ Enable CloudTrail logging for S3 API calls

**Implementation Notes:**
- **AWS Signature V4**: Full implementation of AWS Signature Version 4 signing algorithm
- **HMAC-SHA256**: Uses libsec (Plan 9) or OpenSSL (POSIX) for cryptographic operations
- **URL Encoding**: Automatic URL encoding for object keys with special characters
- **SNI Support**: Server Name Indication for TLS connections to S3 endpoints
- **Payload Hashing**: SHA-256 hashing of request payloads for signature calculation
- **Timestamp**: Uses UTC time in ISO8601 format for request signing
- **Error Handling**: Returns `nil` or `false` on errors (network, auth, S3 errors)

## Performance

NaN boxing is enabled by default for better performance. This uses a clever bit manipulation technique to store type information within IEEE 754 double values, reducing memory usage and improving cache performance.

To disable NaN boxing, edit `common.h` and comment out:
```c
#define NAN_BOXING
```

## Usage Tips

1. **Flexible concatenation**: Mix any type with strings using `+` (automatic conversion)
2. **File I/O**: Use built-in functions for reading/writing files and managing directories
3. **JSON support**: Parse and serialize JSON for data persistence and configuration files
4. **HTTP/HTTPS client**: Make GET/POST/PUT requests to APIs and web services (both HTTP and HTTPS on both platforms)
5. **HTTP server**: Start a basic HTTP server with `httpServer(port)` for local development and testing
6. **AWS S3 integration**: Direct S3 access with `s3ListObjects`, `s3GetObject`, `s3PutObject` using AWS credentials or STS tokens
7. **Error handling**: File, JSON, HTTP, and S3 operations return `nil` or `false` on failure - always check return values
8. **Arrays**: Native array support with bracket syntax - create with `[1, 2, 3]`, access with `arr[0]`, get size with `arr.length`. **Important**: Arrays have fixed size - you can only modify existing indices, not add new ones. To build arrays dynamically, pre-allocate with nil values.
9. **Dict class**: Use the native `Dict` class for efficient string→string maps and caches (`put`, `get`, `has`, `remove`, `size`, `clear`, `iter()`).
10. **No exceptions**: Use return values to indicate success/failure
11. **Global scope**: All functions and classes are global
12. **Numeric addition**: `+` performs addition when both operands are numbers
13. **Truthiness**: `false` and `nil` are falsy, everything else is truthy

## Example Programs

### Hello World
```lux
print "Hello, World!";
```

### Fibonacci
```lux
fun fib(n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

print fib(10);  // 55
```

### Class with Method Chaining
```lux
class Counter {
    init() {
        this.value = 0;
    }

    add(n) {
        this.value = this.value + n;
        return this;
    }

    get() {
        return this.value;
    }
}

var c = Counter();
print c.add(5).add(3).get();  // 8
```

### File I/O Example
```lux
// Write data to a file
var data = "User: Alice\nScore: 100\n";
if (writeFile("score.txt", data)) {
    print "Score saved!";
}

// Read it back
var saved = readFile("score.txt");
if (saved != nil) {
    print "Loaded data:";
    print saved;
}

// Append more data
appendFile("score.txt", "User: Bob\nScore: 95\n");

// List files in directory
createDir("saves");
writeFile("saves/game1.txt", "Level 1 complete");
writeFile("saves/game2.txt", "Level 2 complete");

var files = listDir("saves");
print "Save files:";
print files;
```

### Using Objects as Dictionaries
```lux
// Lux has no built-in dictionary/hashmap type,
// but you can use object properties for key-value storage

// Simple dictionary
class Dict {
    init() {}
}

var config = Dict();
config.host = "localhost";
config.port = 8080;
config.debug = true;

print "Server: " + config.host + ":" + config.port;

// Nested dictionaries for complex data
var user = Dict();
user.name = "Alice";
user.age = 30;

var address = Dict();
address.street = "123 Main St";
address.city = "Portland";
address.zip = "97201";

user.address = address;

print user.name + " lives in " + user.address.city;

// Useful for JSON work - build complex structures
var gameData = Dict();
gameData.player = "PlayerOne";
gameData.level = 5;
gameData.score = 1500;

var inventory = Dict();
inventory.gold = 100;
inventory.potions = 3;
inventory.keys = 1;

gameData.inventory = inventory;

// Serialize to JSON for saving
var json = toJSON(gameData);
if (json != nil) {
    writeFile("game.json", json);
    print "Game saved!";
}

// Load and access nested data
var loaded = readFile("game.json");
if (loaded != nil) {
    var data = parseJSON(loaded);
    print "Welcome back, " + data.player;
    print "Gold: " + data.inventory.gold;
}
```

## Learning Resources

- **[SPEC.md](SPEC.md)** — language definition (grammar, types, operators, natives)
- **tutorial.lux** - Start here for a guided tour of all features
- **examples.lux** - Practical examples including bank accounts, game characters, and data structures
- **quickref.lux** - Syntax cheatsheet and common patterns
- **benchmark.lux** - Performance testing

## Implementation Details

- **Bytecode VM**: Compiles to bytecode for efficient execution
- **Single-pass compiler**: Fast compilation with minimal memory overhead
- **Mark-and-sweep GC**: Automatic memory management with stress testing support
- **Upvalue closures**: Proper closure support with heap allocation when needed
- **Method binding**: Fast method invocation with bound method optimization

## Architecture

```
Source Code (.lux)
    ↓
Scanner (tokens)
    ↓
Compiler (bytecode)
    ↓
VM (execution)
```

## Testing

Run the test suite:
```
8.out chapter28_test.lux
8.out ch29-inherit.lux
```

## Credits

Based on "Crafting Interpreters" by Robert Nystrom
Ported to Plan 9 C with NaN boxing optimization

## License

All code here fall under MIT License:

	Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.

