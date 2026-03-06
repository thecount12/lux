# Lux Programming Language

A dynamically-typed scripting language based on the Lox language from "Crafting Interpreters" by Robert Nystrom, implemented in Plan 9 C. However we do have some new features. File IO, Json serialization, HTTP/HTTPS client functionality (GET, POST and PUT), and HTTP server capabilities.

## Features

- **Dynamic typing** - Variables can hold any type
- **First-class functions** - Functions are values that can be passed around
- **Closures** - Functions can capture and remember their surrounding scope
- **Classes and inheritance** - Object-oriented programming with single inheritance
- **Automatic memory management** - Mark-and-sweep garbage collector
- **NaN boxing** - Optional performance optimization for value representation

## Language Features

### Basic Types
- **Numbers**: `42`, `3.14` (all numbers are doubles)
- **Strings**: `"hello"`, `"world"`
- **Booleans**: `true`, `false`
- **Nil**: `nil`

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

## Running

### Interactive REPL:
```
8.out
```

### Run a file:
```
8.out script.lux
```

### Example files:
```
8.out getting_started.lux           # START HERE - Learn the basics
8.out demo.lux                      # Simple working demo
8.out test_concat.lux               # String concatenation examples
8.out tests/test_fileio.lux         # File I/O examples
8.out tests/test_fileops.lux        # Complete file operations test
8.out tests/test_json.lux           # JSON parsing and serialization
8.out tests/test_http_plan9.lux     # HTTP client (Plan 9 HTTP)
8.out tests/test_https.lux          # HTTPS client with TLS (Plan 9)
posix/my_program tests/test_http.lux  # HTTP/HTTPS examples (POSIX)
8.out tests/test_http_server.lux    # HTTP server (both Plan 9 and POSIX)
8.out ch29-inherit.lux              # Inheritance examples
8.out closure.lux                   # Closure examples
```

## String Concatenation

Lux now supports **automatic type conversion** with the `+` operator:
- **Numbers**: `1 + 2` → `3` (numeric addition)
- **Strings**: `"Hello" + " World"` → `"Hello World"`
- **Mixed types**: `"Score: " + 42` → `"Score: 42"` (auto-converts to string)
- **Multiple types**: `"Player " + 1 + " wins!"` → `"Player 1 wins!"`

Any type (number, boolean, nil) is automatically converted to string when used with `+` and at least one string operand.

## Built-in Functions

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
6. **Error handling**: File, JSON, and HTTP operations return `nil` or `false` on failure - always check return values
7. **Array access**: JSON arrays become objects with numeric string keys - access with `arr.0`, `arr.1`, etc.
8. **Dictionary pattern**: Use object properties for key-value storage (no built-in hashmap/dictionary type)
9. **No exceptions**: Use return values to indicate success/failure
10. **Global scope**: All functions and classes are global
11. **Numeric addition**: `+` performs addition when both operands are numbers
12. **Truthiness**: `false` and `nil` are falsy, everything else is truthy

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

