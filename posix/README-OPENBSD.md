# Building Lux on OpenBSD

The POSIX version of Lux builds on OpenBSD with GNU make and the ports-provided crypto/network libraries.

## Prerequisites

Install required tools and libraries:

```sh
doas pkg_add gmake curl openssl
```

Optional database package:

```sh
doas pkg_add sqlite3
```

## Build

Use `gmake` (GNU make), not BSD `make`.

```sh
cd posix
gmake clean
gmake
```

To enable SQLite support:

```sh
gmake clean
gmake USE_SQLITE=1
```

## Run

```sh
./lux ../examples/demo.lux
./lux ../tests/test_json.lux
```

## Notes

- The OpenBSD Makefile path settings use:
  - Includes: `/usr/local/include`
  - Libraries: `/usr/local/lib`
- Warnings about unused variables/functions do not block the build.

## Troubleshooting

**Command not found: gmake**

Install GNU make:

```sh
doas pkg_add gmake
```

**curl/openssl headers or libraries not found**

Install or reinstall the packages:

```sh
doas pkg_add curl openssl
```

Then rebuild:

```sh
gmake clean && gmake
```

**SQLite link errors with `USE_SQLITE=1`**

Install SQLite development package:

```sh
doas pkg_add sqlite3
```
