# Lux Programming Language

A dynamically-typed scripting language based on the Lox language from "Crafting Interpreters" by Robert Nystrom, implemented in Plan 9 C.

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
8.out getting_started.lux  # START HERE - Learn the basics
8.out demo.lux             # Simple working demo
8.out test_concat.lux      # String concatenation examples
8.out tests/test_fileio.lux    # File I/O examples
8.out tests/test_fileops.lux   # Complete file operations test
8.out tests/test_json.lux      # JSON parsing and serialization
8.out ch29-inherit.lux     # Inheritance examples
8.out closure.lux          # Closure examples
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
4. **Error handling**: File and JSON operations return `nil` or `false` on failure - always check return values
5. **Array access**: JSON arrays become objects with numeric string keys - access with `arr.0`, `arr.1`, etc.
6. **Dictionary pattern**: Use object properties for key-value storage (no built-in hashmap/dictionary type)
7. **No exceptions**: Use return values to indicate success/failure
8. **Global scope**: All functions and classes are global
9. **Numeric addition**: `+` performs addition when both operands are numbers
10. **Truthiness**: `false` and `nil` are falsy, everything else is truthy

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

See the original Crafting Interpreters book for language design.
Implementation adapted for Plan 9.
