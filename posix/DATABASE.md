# Database Support in Lux (POSIX)

Lux supports **optional** database connectivity for SQLite, PostgreSQL, MySQL, and Oracle. All database features use **conditional compilation** to keep the Lux binary small and lightweight when you don't need them.

## Philosophy: Keep It Small

By default, **no database support is compiled in**. This keeps the Lux binary minimal with zero database dependencies. Enable only the databases you actually need!

## Quick Start

### 1. Install Database Libraries (as needed)

Install only the client **headers and libs** for the backends you enable. Enabling `USE_POSTGRES=1` without `libpq` headers fails with `libpq-fe.h: No such file or directory`.

**Ubuntu/Debian** (`apt`):
```bash
sudo apt-get update
sudo apt-get install libsqlite3-dev libpq-dev libmysqlclient-dev
```

**Amazon Linux / Fedora / RHEL** (`dnf` / `yum`):
```bash
sudo dnf install sqlite-devel libpq-devel mysql-devel
# Older Amazon Linux 2:
# sudo yum install sqlite-devel postgresql-devel mysql-devel
```

**macOS** (Homebrew):
```bash
brew install sqlite3 postgresql mysql
```

**Oracle** (requires Oracle Instant Client, not a distro `-dev` package):

`oci.h` is **not** in apt as `libpq-dev` is. You need Instant Client **Basic + SDK**.

1. See if it is already on the machine:
```bash
find /usr /opt "$HOME" -name oci.h 2>/dev/null
find /usr /opt "$HOME" -name 'libclntsh.so*' 2>/dev/null
```
2. If you find it, pass that tree into `make`:
```bash
# Zip layout (oci.h under sdk/include):
make USE_ORACLE=1 ORACLE_HOME=/path/to/instantclient_21_15
# RPM/deb layout is auto-detected under /usr/include/oracle/*/client64
```
3. If it is missing, unzip Basic + SDK from
   https://www.oracle.com/database/technologies/instant-client.html
```bash
sudo mkdir -p /opt/oracle
sudo unzip instantclient-basic-linux.x64-*.zip -d /opt/oracle
sudo unzip instantclient-sdk-linux.x64-*.zip -d /opt/oracle
ls /opt/oracle/instantclient_*/sdk/include/oci.h
make USE_ORACLE=1 ORACLE_HOME=/opt/oracle/instantclient_21_15   # match the unzipped dir
```
Without the SDK, omit Oracle: `make USE_SQLITE=1 USE_POSTGRES=1 USE_MYSQL=1`

### 2. Enable Databases in Makefile

Open `posix/Makefile` and set the databases you want to use:

```make
# Database configuration (set to 1 to enable, 0 to disable)
USE_SQLITE ?= 1      # Enable SQLite
USE_POSTGRES ?= 1    # Enable PostgreSQL
USE_MYSQL ?= 1       # Enable MySQL
USE_ORACLE ?= 0      # Keep Oracle disabled
```

### 3. Compile Lux

```bash
cd posix
make clean
make
```

The binary will only include the databases you enabled!

## Database API

### Connect to Database

```lux
// SQLite - embedded database, no server needed
var conn = dbConnect("sqlite", "path/to/database.db");

// PostgreSQL - connection string format
var conn = dbConnect("postgres", "host=localhost dbname=mydb user=myuser password=mypass");

// MySQL - semicolon-separated format
var conn = dbConnect("mysql", "host=localhost;user=root;password=root;database=mydb");

// Oracle - user/pass@host:port/service format
var conn = dbConnect("oracle", "myuser/mypass@localhost:1521/XEPDB1");
```

### Execute Queries

```lux
// Returns array of Dict objects (one per row)
var results = dbQuery(conn, "SELECT * FROM users WHERE age > 25");

if (results != nil) {
    for (var i = 0; i < len(results); i = i + 1) {
        var row = results[i];
        print(row.name + " - " + row.email);
    }
}

// INSERT/UPDATE/DELETE also use dbQuery
dbQuery(conn, "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
```

### Close Connection

```lux
dbClose(conn);
```

## Examples

See `examples/database_example.lux` for complete examples of:
- SQLite local development
- PostgreSQL production usage
- MySQL web applications
- Oracle enterprise queries
- Analytics and aggregate queries

## Connection String Formats

### SQLite
```
"path/to/database.db"
```
- Relative or absolute path
- Creates database if it doesn't exist

### PostgreSQL
```
"host=HOST dbname=DATABASE user=USERNAME password=PASSWORD port=PORT"
```
- Space-separated key=value pairs
- Port defaults to 5432 if omitted

### MySQL
```
"host=HOST;user=USERNAME;password=PASSWORD;database=DATABASE;port=PORT"
```
- Semicolon-separated key=value pairs
- Port defaults to 3306 if omitted

### Oracle
```
"username/password@host:port/service_name"
```
- Traditional Oracle connection string format
- Requires Oracle Instant Client

## Building for Different Scenarios

### Minimal Build (no databases)
```bash
make clean
make
```

### Development Build (SQLite only)
```bash
make clean
make USE_SQLITE=1
```

### Web Application Build (PostgreSQL + MySQL)
```bash
make clean
make USE_POSTGRES=1 USE_MYSQL=1
```

### Enterprise Build (all databases)
```bash
make clean
make USE_SQLITE=1 USE_POSTGRES=1 USE_MYSQL=1 USE_ORACLE=1
```

## Binary Size Impact

Approximate size increases when enabling databases (x86_64 macOS):

| Configuration | Binary Size | Increase |
|--------------|-------------|----------|
| No databases | ~200 KB | baseline |
| SQLite only | ~300 KB | +100 KB |
| +PostgreSQL | ~450 KB | +250 KB |
| +MySQL | ~600 KB | +400 KB |
| +Oracle | ~800 KB | +600 KB |
| All databases | ~1.2 MB | +1 MB |

**Tip:** Only compile what you need to keep deployments lean!

## Error Handling

All database functions return `nil` on error:

```lux
var conn = dbConnect("postgres", "invalid connection string");
if (conn == nil) {
    print("Failed to connect!");
    return;
}

var results = dbQuery(conn, "INVALID SQL");
if (results == nil) {
    print("Query failed!");
}
```

## Thread Safety

Database connections are **not thread-safe**. Create separate connections per thread/coroutine when parallel processing is needed.

## Best Practices

1. **Always close connections** when done
2. **Use parameterized queries** to prevent SQL injection (concatenate safely)
3. **Handle nil results** from failed queries
4. **Choose the right database** for your use case:
   - SQLite: Embedded apps, local dev, single user
   - PostgreSQL: Production web apps, complex queries
   - MySQL: High-traffic web applications
   - Oracle: Enterprise applications with existing Oracle infrastructure

## Troubleshooting

### `libpq-fe.h: No such file or directory` (header is installed)

Debian/Ubuntu install the header at `/usr/include/postgresql/libpq-fe.h`, not `/usr/include/libpq-fe.h`. Confirm:

```bash
ls /usr/include/postgresql/libpq-fe.h
pkg-config --cflags libpq
```

The POSIX `Makefile` adds that include path via `pkg-config` (or `-I/usr/include/postgresql`). Rebuild after updating `posix/Makefile`.

### `oci.h: No such file or directory`

The default path `/opt/oracle/instantclient_21_1/sdk/include/oci.h` is not present. Instant Client is not an apt package. Search first:

```bash
find /usr /opt "$HOME" -name oci.h 2>/dev/null
```

If found, rebuild with `ORACLE_HOME` pointing at the Instant Client directory (the parent of `sdk/` or `libclntsh.so`). If not found, either install Basic+SDK or drop `USE_ORACLE=1`.

### "Failed to connect" error

- Verify the database server is running
- Check connection credentials
- Confirm network/firewall settings
- For Oracle: Ensure `ORACLE_HOME` is set correctly

### Linker errors during compilation

- Install missing database libraries
- Check library paths in Makefile
- On macOS: Use `brew install <database>`
- On Linux: Use `apt-get install lib<database>-dev`

### "dbConnect is not defined" error

- Database support wasn't compiled in
- Set `USE_<DATABASE>=1` in Makefile and recompile

## Platform Support

- **macOS**: Full support for all databases
- **Linux**: Full support for all databases
- **Plan 9**: Not supported (POSIX only)

## Additional Resources

- SQLite: https://www.sqlite.org/
- PostgreSQL: https://www.postgresql.org/
- MySQL: https://www.mysql.com/
- Oracle: https://www.oracle.com/database/
