# 🚀 LUX
**The Lox language, enlightened for Plan 9.**

[View on GitHub](https://github.com/thecount12/lux) • [Download v1.0.0](https://github.com/thecount12/lux/releases)

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

## ☁️ Cloud-Ready & AWS Friendly

Lux is designed for the modern web. Its small footprint and strict, warning-free C core make it an ideal candidate for cloud deployment and serverless architectures.

### ⚡ Seamless AWS Integration
*   **AWS Lambda Optimized:** Lux’s fast startup times and minimal memory overhead allow you to run Lux scripts as lightweight, high-performance Lambda functions.
*   **Portable Binaries:** Build Lux once and deploy it across AWS EC2 instances, Fargate containers, or even within a custom Lambda runtime.
*   **Infrastructure as Code:** Since Lux is easily managed via `make` or `mk`, it integrates perfectly into CI/CD pipelines using **AWS CodeBuild** or **GitHub Actions**.

## 💻 See it in Action

Lux supports SQLite, PostgresSQL, MySQL, and Oracle

```lux
fun sqliteExample() {
    print("=== SQLite Example ===");
    
    // Connect to SQLite database (creates if not exists)
    var conn = dbConnect("sqlite", "test.db");
    
    if (conn == nil) {
        print("Failed to connect to SQLite");
        return;
    }
    
    print("Connected to SQLite database");
    
    // Create table
    dbQuery(conn, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)");
    
    // Insert data
    dbQuery(conn, "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
    dbQuery(conn, "INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')");
    dbQuery(conn, "INSERT INTO users (name, email) VALUES ('Charlie', 'charlie@example.com')");
    
    // Query data
    var results = dbQuery(conn, "SELECT * FROM users");
    
    if (results != nil) {
        print("Query returned " + len(results) + " rows:");
        for (var i = 0; i < len(results); i = i + 1) {
            var row = results[i];
            print("  ID: " + row.id + ", Name: " + row.name + ", Email: " + row.email);
        }
    }
    
    // Close connection
    dbClose(conn);
    print("SQLite connection closed");
}
```
