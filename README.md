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

## Performance

NaN boxing is enabled by default for better performance. This uses a clever bit manipulation technique to store type information within IEEE 754 double values, reducing memory usage and improving cache performance.

To disable NaN boxing, edit `common.h` and comment out:
```c
#define NAN_BOXING
```

## Usage Tips

1. **Flexible concatenation**: Mix any type with strings using `+` (automatic conversion)
2. **No built-in collections**: Use object properties or create your own linked structures
3. **No exceptions**: Use return values to indicate success/failure
4. **Global scope**: All functions and classes are global
5. **Numeric addition**: `+` performs addition when both operands are numbers
6. **Truthiness**: `false` and `nil` are falsy, everything else is truthy

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
