# Lux

A portable scripting language for system automation, HTTP services, and cloud work. Full [Lox](https://craftinginterpreters.com/) parity, native on **Plan 9**, and the same runtime on **macOS, Linux, and OpenBSD**.

[View on GitHub](https://github.com/thecount12/lux) · [Download v1.1.0](https://github.com/thecount12/lux/releases/tag/v1.1.0) · [README](https://github.com/thecount12/lux#readme)

---

## What is Lux?

Lux started as a Plan 9 port of Lox and grew into a small bytecode interpreter with a real standard library: files, JSON, HTTP client and server, templates, Markdown, databases, and AWS. It compiles with `mk`/`8c` on Plan 9 and `make` on POSIX.

## Features

**Language**
- Dynamic typing — numbers, strings, arrays, hashes, objects
- First-class functions — lexical scoping, closures, recursion
- Classes and single inheritance
- Modules — `import "path.lux"`
- Mark-and-sweep garbage collector

**Standard library**
- **HTTP** — client (`httpGet` / `httpPost` / `httpPut` / `httpRequest`) and `Server` (routes, static files, virtual hosts, middleware, prefork workers)
- **Templates** — native `renderTemplate` (`{{ }}`, `{% for %}`, `{% include %}`, `{% include_md %}`)
- **Markdown** — `markdownToHtml` and `renderMarkdown`
- **File I/O** — read, write, append, directories
- **JSON / XML** — parse and serialize
- **Dict** — `put` / `get` / `has` / `remove` / `iter`
- **Crypto & AWS** — SHA-256, HMAC, request signing, S3 / STS / IAM
- **Databases** — SQLite, PostgreSQL, MySQL (opt-in)
- **Tools** — `luxfmt`, `luxtest`, `luxcheck`, `luxlint`
- **Runtime** — `args()`, `exit()`, `typeof()`, `run()`

## Hello

```lux
print "Hello, Lux!";

fun fib(n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

print fib(10);  // 55
```

```bash
./lux -c "print 1 + 2;"
```

## HTTP server

Routes, static files, virtual hosts, middleware, and prefork workers — Plan 9 and POSIX.

```lux
fun handleHello(req, res) { res.send("Hello from Lux!"); }

var server = Server(8080);
server.workers(4);
server.get("/hello", handleHello);
server.static("public");
server.start();
```

```lux
var res = httpGet("https://fakestoreapi.com/products/1");
if (res != nil) {
    var data = parseJSON(res);
    print data.title;
}
```

See `examples/server_middleware.lux`, `examples/vhost_server.lux`, and `examples/template_server/`.

## Templates and Markdown

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

```lux
var html = markdownToHtml("# Hi\n\nHello *world*.");
```

## Install

```bash
git clone https://github.com/thecount12/lux.git
cd lux
```

**macOS / Linux**

```bash
cd posix
make
./lux ../examples/demo.lux
```

**OpenBSD** — `gmake` instead of `make`.

**Plan 9**

```bash
mk
./8.out examples/demo.lux
```

Optional database drivers and platform notes: [BUILD.md](https://github.com/thecount12/lux/blob/front/BUILD.md).

## Cloud

Small footprint, HTTPS client, and native AWS helpers (S3, STS, IAM, request signing). Use it for scripts, Lambda-style jobs, or CI — not as a heavy numerical runtime.

## Docs

The full language reference, HTTP server notes, and examples live in the [GitHub README](https://github.com/thecount12/lux#readme).
