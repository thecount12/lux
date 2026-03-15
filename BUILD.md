# Building Lux

## POSIX (macOS, Linux, OpenBSD)

Navigate to the `posix/` directory:

```bash
cd posix
```

### Build Targets

```bash
make              # Build lux interpreter (default)
make luxfmt       # Build code formatter
make luxtest      # Build test runner
make luxdoc       # Build documentation generator
make clean        # Clean object files and binaries
```

### macOS (Homebrew)

Install dependencies:

```bash
brew install curl openssl@3
```

Build:

```bash
cd posix
make
./lux ../examples/demo.lux
```

To run code with SQLite support:

```bash
make USE_SQLITE=1
./lux -d # or any .lux file
```

### Linux (Debian/Ubuntu)

Install dependencies:

```bash
apt-get install build-essential libcurl4-openssl-dev libssl-dev
# Optional database drivers:
apt-get install libsqlite3-dev libpq-dev libmysqlclient-dev
```

Build:

```bash
cd posix
make
./lux ../examples/demo.lux
```

### Linux (RPM-based: Fedora, CentOS, RHEL)

Install dependencies:

```bash
yum groupinstall "Development Tools"
yum install curl-devel openssl-devel
# Optional:
yum install sqlite-devel postgresql-devel mariadb-devel
```

Build:

```bash
cd posix
make
./lux ../examples/demo.lux
```

### OpenBSD

OpenBSD requires **GNU Make** (Plan 9 mk is not available on OpenBSD). Install:

```bash
pkg_add gmake curl openssl
```

Build:

```bash
cd posix
gmake
./lux ../examples/demo.lux
```

**Note:** The Makefile auto-detects OpenBSD and sets correct flags:
- Includes: `-I/usr/local/include`
- Libraries: `-L/usr/local/lib`

See [posix/README-OPENBSD.md](posix/README-OPENBSD.md) for detailed notes.

## Plan 9

Plan 9 uses the `mk` build tool (not Make). From the root directory:

```bash
mk              # Compile and link all sources → 8.out
mk clean        # Clean .8 files and binaries
```

The resulting binary is `8.out`. Run:

```bash
./8.out examples/demo.lux
```

### Building Tools on Plan 9

**luxfmt (code formatter):**
```bash
6c luxfmt_plan9.c
6l -o luxfmt luxfmt_plan9.6
./luxfmt examples/demo.lux
```

**luxtest (test runner):**
```bash
6c luxtest_plan9.c
6l -o luxtest luxtest_plan9.6
./luxtest tests/
```

Using `$O` and `$objtype` variables (more portable):
```bash
$objtype^c luxfmt_plan9.c
$objtype^l -o luxfmt luxfmt_plan9.$O
```

### Plan 9 Compiler Notes

- **8c**: Intel x86-64 compiler
- **6c**: AMD64 compiler (Plan 9's x86-64, also used on other arches in emulation)
- **$objtype**: Machine type variable (e.g., `386`, `amd64`, `arm`)
- **$O**: Compiled object suffix (e.g., `.8`, `.6`)

The mkfile detects your `$objtype` and builds accordingly. You can also manually specify:

```bash
objtype=386 mk   # Force 32-bit
objtype=amd64 mk # Force 64-bit
```

## Optional Features: Database Support

By default, Lux builds without database support to keep the binary lightweight. To enable:

### macOS (SQLite3)
```bash
cd posix
make USE_SQLITE=1
./lux script.lux
```

### Linux (SQLite3, PostgreSQL, MySQL)
```bash
cd posix
make USE_SQLITE=1 USE_POSTGRES=1 USE_MYSQL=1
./lux script.lux
```

Then in Lux:
```lux
var db = dbConnect("sqlite3", "file.db");
var result = dbQuery(db, "SELECT * FROM users;");
dbClose(db);
```

## Verifying Installation

After building, verify with:

```bash
# POSIX
cd posix
./lux -c "print 1 + 2;"   # Interactive: press Ctrl+D to exit
echo 'print "Hello";' > /tmp/test.lux && ./lux /tmp/test.lux

# Plan 9
./8.out -c "print 1 + 2;"
echo 'print "Hello";' > /tmp/test.lux && ./8.out /tmp/test.lux
```

## Running the Test Suite

Lux includes a comprehensive test suite:

```bash
# POSIX
cd posix
make luxtest
./luxtest ../tests/

# Plan 9
6c luxtest_plan9.c && 6l -o luxtest luxtest_plan9.6
./luxtest tests/
```

Options:
```bash
./luxtest -t 10 tests/       # 10-second timeout per test (default: 10s)
./luxtest -v tests/          # Verbose (show stderr for passing tests)
./luxtest -t 0 tests/        # No timeout
./luxtest tests/fib.lux      # Single test file
```

## Troubleshooting

### Missing `curl` or `libcrypto`

**macOS:**
```bash
brew install curl openssl@3
# May need to link:
export LDFLAGS="-L/usr/local/opt/openssl@3/lib"
export CPPFLAGS="-I/usr/local/opt/openssl@3/include"
```

**Linux:** Install dev packages (`libcurl4-openssl-dev`, `libssl-dev`).

**OpenBSD:**
```bash
pkg_add curl openssl
```

### Makefile Errors (OpenBSD)

Ensure you're using GNU Make (`gmake`), not BSD Make:

```bash
which gmake     # Should be /usr/local/bin/gmake
gmake -v        # Should show "GNU Make"
```

### Plan 9 Build Errors

Often due to whitespace in mkfile (tabs vs spaces). Plan 9's `mk` is strict:
- Recipe lines in mkfile must start with a **TAB**, not spaces
- Comments use `#`

If you see "missing separator", check the mkfile tabs.

## Building on Other Unix-like Systems

For systems not explicitly documented (BSD variants, illumos, etc.):

1. Install dependencies: `gcc`, `make`, `curl`, `openssl`
2. Try the POSIX build:
   ```bash
   cd posix
   make
   ```

If compilation fails, check:
- Compiler flags in `posix/Makefile` (may need adjustment)
- Header locations (`-I/path/to/openssl/include`, etc.)
- Library paths (`-L/path/to/openssl/lib`, etc.)

Consider filing an issue with build output.

## Clean POSIX Build (Minimal Dependencies)

To build without cryptography, HTTP, or AWS support:

```bash
cd posix
# Comment out crypto/HTTP parts in vm.c
gcc -Wall -Wextra -std=c99 -g -c *.c
gcc *.o -o lux -lcurl  # Only needs curl, no libcrypto
```

This trades features for minimal dependencies. Not recommended for production use.

## Development Workflow

1. Make changes locally
2. Test on POSIX (macOS/Linux):
   ```bash
   cd posix && make && ./luxtest ../tests/
   ```
3. Copy changes to Plan 9 environment
4. Rebuild and test on Plan 9:
   ```bash
   mk && ./luxtest tests/
   ```
5. Commit and push to GitHub

For iterative development, maintain a unified source tree and sync between platforms.
