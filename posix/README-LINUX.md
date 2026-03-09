# Building Lux on Linux

The POSIX version of Lux works on Linux with standard system libraries.

## Prerequisites

Install the required development libraries:

### Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install build-essential libcurl4-openssl-dev libssl-dev
```

### Fedora/RHEL/CentOS:
```bash
sudo dnf install gcc make libcurl-devel openssl-devel
# Or on older systems:
sudo yum install gcc make libcurl-devel openssl-devel
```

### Arch Linux:
```bash
sudo pacman -S base-devel curl openssl
```

## Building

```bash
cd posix
make clean
make
```

The Makefile automatically detects Linux and uses appropriate compiler flags.

## Running

```bash
./lux ../examples/demo.lux
./lux ../tests/test_s3.lux
```

## Features

All features work on Linux:
- ✅ Arrays with bracket syntax `[1, 2, 3]`
- ✅ Array indexing `arr[i]`
- ✅ Array `.length` property
- ✅ HTTP/HTTPS client with libcurl
- ✅ AWS S3 operations with Signature V4
- ✅ JSON parsing and serialization
- ✅ File I/O operations
- ✅ XML parsing
- ✅ REPL discovery via `help()` and `help("name")`

## Troubleshooting

**OpenSSL not found:**
```bash
# Check if OpenSSL is installed
openssl version
pkg-config --modversion openssl

# If not found, install openssl-devel (Fedora) or libssl-dev (Ubuntu)
```

**libcurl not found:**
```bash
# Check if libcurl is installed
curl-config --version

# If not found, install libcurl4-openssl-dev (Ubuntu) or libcurl-devel (Fedora)
```

**Linking errors:**
If you get SSL/crypto linking errors, you may need to explicitly add library paths:
```bash
export LDFLAGS="-L/usr/lib/x86_64-linux-gnu"
make clean && make
```

## Architecture

Linux builds are native to your system architecture (x86_64, ARM, etc.) - no Rosetta translation needed.
