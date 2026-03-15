# Lux — A Portable Scripting Language

Lux is a dynamically-typed scripting language for system automation, cloud integration, and embedded scripting. Originally written in Plan 9 C, it runs on **macOS, Linux, OpenBSD, and Plan 9**.

Built from first principles (inspired by "Crafting Interpreters"), Lux combines a clean language design with modern features: HTTP/HTTPS, cryptography, AWS integration, databases, and file I/O.

## Features

**Language:**
- **Dynamic typing** — numbers, strings, arrays, hashes, objects
- **First-class functions** — lexical scoping, closures, recursion
- **Classes & inheritance** — single inheritance, methods, fields
- **Modules** — `import "path.lux"` for code organization
- **Automatic GC** — mark-and-sweep garbage collector

- **Standard Library:**
- **File I/O** — read, write, append, list directories
- **JSON** — parse and serialize
- **XML** — basic parsing
- **Strings/Arrays** — slice, find, split, sort, binary search, array concatenation (`+`)
- **Dictionaries** — native `Dict` class with `put`/`get`/`has`/`remove`/`size`/`clear` plus `iter()` returning an array of `{key, value}` entries
- **Float64Array** — typed double buffer, dot product (POSIX)
- **HTTP** — client (GET/POST/PUT) and server
- **Crypto** — SHA-256, HMAC-SHA256, AWS request signing
- **Cloud** — AWS S3, STS, IAM operations
- **Databases** — SQLite, PostgreSQL, MySQL (opt-in)
- **Code formatting** — `luxfmt` (gofmt-style)
- **Testing** — `luxtest` with timeouts and reporting
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
- Large numerical compute — Lux is not tuned for large-scale number‑crunching; prefer numerical Python/Julia for heavy workloads. For moderate numeric work you can use Float64Array (POSIX), BLAS wrappers, or build the POSIX runtime with optimizations (see `posix/Makefile` and `benchmark.md`).
- GUI development (no graphics APIs)
- Mobile development (not designed for mobile targets)

## Practical Examples

### HTTP Client
```lux
// GET request
var resp = httpGet("https://api.example.com/users");
print resp.statusCode;
var data = parseJSON(resp.body);
print data["name"];

// POST with custom headers
var headers = {"Authorization": "Bearer token123", "Content-Type": "application/json"};
var body = toJSON({"username": "alice", "email": "alice@example.com"});
var resp = httpRequest("POST", "https://api.example.com/users", body, headers);
if (resp.statusCode == 201) {
    print "User created!";
}
```

### HTTP Server
```lux
// Start a simple HTTP server on port 8080
httpServer(8080);
// Handles GET /, GET /echo, POST /echo automatically
// Try: curl http://localhost:8080/
// Try: curl -X POST http://localhost:8080/echo -d 'hello'
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
```

### Logical operators and ternary

- `|` — logical OR (short-circuit). Example: `if (a | b) { ... }`
- `&` — logical AND (short-circuit). Example: `if (a & b) { ... }`
- Ternary conditional: `cond ? exprTrue : exprFalse` returns a value and can be used inside expressions.

Note: these are logical (short-circuit) operators, not bitwise. `||` and `&&` are not currently aliases.

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
var table = {};
var i = 0;
while (i < entries.length) {
  var entry = entries[i];
  table[entry.key] = parseJSON(entry.value);
  i = i + 1;
}

print toJSON(table);
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
8.out tests/c29-inherit.lux             # Inheritance examples
8.out tests/closure.lux                 # Closure examples
```

**Note**: Lux has a limit of 256 constants per source file. Very large files with many string literals may hit this limit.

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

`help()` groups callables by category (Core, File and Directory, String and Array, Data Formats, HTTP, Crypto, AWS, Database, and User or Other) so long lists are easier to scan.

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

### JSON Operations

Lux includes native JSON parsing and serialization for easy data interchange.

#### `parseJSON(jsonString)` → value or nil
Parses a JSON string and returns a Lux value. JSON objects become Lux class instances with properties, and JSON arrays become objects with numeric string keys ("0", "1", "2", etc.) plus a `length` property.

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
    print arr.0;      // 1
    print arr.4;      // 5
}

// Nested objects
var nested = "{\"user\":{\"name\":\"Bob\",\"level\":5}}";
var data = parseJSON(nested);
print data.user.name;  // Bob
```

#### `toJSON(value)` → string or nil
Converts a Lux value to a JSON string. Supports numbers, strings, booleans, nil, and objects (class instances).

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

### Float64Array (POSIX)

**Platform**: POSIX only (macOS, Linux, OpenBSD). Not available on Plan 9.

Float64Array provides a typed double-precision buffer for numerical work. Use it for dot products, moving averages, or when you need faster element access than regular Lux arrays.

#### `float64_available()` → bool

Returns `true` if Float64Array natives are available in this build. Use this to conditionally run Float64Array code (e.g. in benchmarks that should work on both POSIX and Plan 9).

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

### HTTP Operations

Lux includes HTTP client functions for making web requests on both Plan 9 and POSIX platforms.

**Platform Support:**
- **Plan 9**: HTTP and HTTPS (uses native `dial()` with libsec TLS)
- **POSIX**: HTTP and HTTPS (uses libcurl)

#### `httpGet(url)` → string or nil
Makes an HTTP GET request and returns the response body as a string.

```lux
// HTTPS example (both platforms)
var response = httpGet("https://api.example.com/data");
if (response != nil) {
    print "Received: " + response;
    
    // Parse JSON responses
    var data = parseJSON(response);
    if (data != nil) {
        print "Status: " + data.status;
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
Makes an HTTP POST request with JSON body and returns the response.

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

#### `Server` class (POSIX) - Routing, Middleware, Static Files
Create a configurable HTTP server with user-defined routes:

```lux
fun handleHello(req, res) { res.send("Hello from Lux!"); }
fun handleData(req, res) {
  var body = parseJSON(req.body);
  res.status(201).json({"status": "received"});
}
fun logger(req, res, next) {
  print "Request: " + req.method + " " + req.path;
  next();
}
var server = Server.new(8080);
server.get("/hello", handleHello);
server.post("/data", handleData);
server.use(logger);
server.static("public");  /* serve files from ./public/ */
server.start();
```

**Controller imports:** Define `server` before importing route modules so they can register routes:
```lux
var server = Server.new(8080);
import "routes/user.lux";   /* routes/user.lux calls server.get(...) */
server.start();
```

**Simple persistence:** Use global variables for in-process state:
```lux
var store = {};
fun postData(req, res) {
  store["last"] = parseJSON(req.body);
  res.status(201).json({"ok": true});
}
fun getData(req, res) { res.json(store); }
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

