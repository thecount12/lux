# 🚀 LUX
**The Lox language, enlightened for Plan 9.**

[View on GitHub](https://github.com) • [Download v1.0.0](https://github.com/releases/tag/v1.0.0)

---

## 🌟 What is Lux?
**Lux** is a feature-rich, high-performance interpreter for the Lox programming language. Originally born from a mission to port Lox to **Plan 9**, it has evolved into a standalone tool with extended features and native support for distributed environments.

## 🪐 First-Class Plan 9 Support
Unlike standard ports, Lux is built to feel native on Plan 9:
*   ✅ **Native Build:** Compiles cleanly with `8c` and `mk`.
*   ✅ **Zero Warnings:** Optimized for strict compiler environments.
*   ✅ **System Integration:** Future-proofed for native Plan 9 system calls.

## ✨ Key Features
*   **Full Lox Parity:** Complete support for closures, classes, and inheritance.
*   **Memory Efficiency:** A custom garbage collector tuned for low-memory systems.
*   **Improved Diagnostics:** Clearer, more descriptive error messages for faster debugging.

## 🛠 Quick Start

### On Plan 9
```bash
git clone https://github.com
cd lux
mk install
```

### On Posix (Linux/MacOS/OpenBSD)
```bash
make && sudo make install
```
