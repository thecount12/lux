# 🚀 Lux v1.0.0 — The "Grand Opening" Release

After 6 months of development, **Lux** is officially hitting **v1.0.0**. What started as a port of the Lox language to Plan 9 has evolved into a feature-rich, and highly portable interpreter.

## 🌟 Why "Lux"?
The name **Lux** (Latin for "light") signifies the evolution of this project. While it maintains full parity with the original Lox specification, it has been "enlightened" with new features, better performance, and first-class support for the **Plan 9** operating system.

## 🛠 What’s New in v1.0.0

### 🪐 Plan 9 Integration
- **Native Port:** Full compatibility with Plan 9 C compilers (`8c`, `5c`, etc.).
- **System Bindings:** Initial support for Plan 9-specific system calls and file patterns.
- **Clean Build:** 0 or minimal warnings during compilation on both POSIX and Plan 9 environments.

### ✨ Extended Features
- **Enhanced Portability:** Refactored core logic to run seamlessly across distributed environments.
- **Optimized Interpreter:** Bytecode improvements for faster execution on resource-constrained systems.
- **Strict Error Handling:** Improved diagnostic messages to simplify debugging in Lux scripts.
- **Dynamic typing:** numbers, strings, arrays, hashes, objects
- **First-class functions:** lexical scoping, closures, recursion
- **Classes & inheritance:** single inheritance, methods, fields
- **Modules:** import "path.lux" for code organization
- **Automatic GC:** mark-and-sweep garbage collector
- **File I/O:** read, write, append, list directories
- **JSON:** parse and serialize
- **XML:** basic parsing
- **Strings/Arrays:** slice, find, split, sort, binary search, array concatenation (+)
- **Dictionaries:** native Dict class with put/get/has/remove/size/clear plus iter() returning an array of {key, value} entries
- **Float64Array:** typed double buffer, dot product (POSIX)
- **HTTP:** client (GET/POST/PUT) and server
- **Crypto:** SHA-256, HMAC-SHA256, AWS request signing
- **Cloud:** AWS S3, STS, IAM operations
- **Databases:** SQLite, PostgreSQL, MySQL (opt-in)
- **Code formatting:** luxfmt (gofmt-style)
- **Testing:** luxtest with timeouts and reporting
- **Assertions:** assert(condition, message) for test-driven development
- **Syntax:** luxcheck fast lightweight syntax checker
- **Linting:** luxlint static checks: unused vars, unreachable code, duplicate functions, shadowed globals

### 📈 Language Parity
- Full support for the complete Lox specification (Closures, Classes, Inheritance, etc.).
- Robust Garbage Collection tuned for low-memory environments.

## ⚠️ Breaking Changes
- **Namespace Migration:** Project references and binaries have been updated from `lox` to `lux`.
- **Build System:** Now requires `mk` on Plan 9 (or `make` on POSIX).

## 📥 Installation
```bash
# On Plan 9:
mk install

# On POSIX:
make && sudo make install
