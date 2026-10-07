# 9P in Lux

9P is Plan 9's file protocol. A remote process looks like a directory: you walk a name, open it, then read or write. Lux can **serve** a small 9P tree and **connect** to one as a client. Same language on POSIX (macOS, Linux, OpenBSD) and Plan 9.

This is not a full filesystem. There is no auth, create, remove, or wstat. The synthetic server is one directory of at most 32 files. You can still copy those files with `export` / `get` / `put`.

Draw, snarf, and plumber are a different branch. This document is only 9P.

## What is on the wire

Each message is:

1. 4-byte little-endian size (including those 4 bytes)
2. 1-byte type (`Tversion` 100, `Tattach` 104, `Twalk` 110, `Topen` 112, `Tread` 116, `Twrite` 118, …)
3. 2-byte tag
4. payload (fids, names, offsets, data)

A **fid** is a handle to a file you have walked to. The client attaches, walks path elements, opens, then reads or writes. Replies are the matching `R*` types, or `Rerror`.

Lux speaks **9P2000**. Message size is 8 KiB.

## Addresses

`listen` and `connect` take the same address strings.

| Address | Meaning |
|---|---|
| `/tmp/lux.9p` | POSIX unix socket |
| `unix!/tmp/lux.9p` | same, Plan 9 dial form |
| `tcp!127.0.0.1!5640` | TCP host and port |
| `tcp!*!5640` | POSIX listen on all interfaces; connect uses `127.0.0.1` |
| `:5640` | POSIX TCP, any interface, port 5640 |
| `127.0.0.1:5640` | POSIX TCP |
| `lux` then `fs.post("lux")` | POSIX `/tmp/lux.9p`; Plan 9 `/srv/lux` |
| `/srv/lux` | Plan 9: open the posted service |

Port 564 is the traditional 9P port and usually needs root. Examples use **5640**.

## Serve a tree from POSIX

```lux
fun readStatus() {
    return "ok\n";
}

fun writeCtl(s) {
    print s;
    return true;
}

var fs = NineP();
fs.file("/status", readStatus, nil);   // read-only
fs.file("/ctl", nil, writeCtl);        // write-only
fs.listen("/tmp/lux.9p");              // blocks
```

`file(path, readFn, writeFn)`:

- `path` is one name under `/` (`/status` is fine; `/a/b` is not)
- `readFn` is `fun () { return "bytes"; }` or `nil`
- `writeFn` is `fun (s) { return true; }` or `nil`
- return `false` from `writeFn` to send `Rerror`

`export(path, localFile)` hangs a real disk file on that 9P name (read and write, up to 8 MiB). Lux has no lambdas, so this is the way to share a handful of files without one `fun` per file.

`listen` and `post` block. Do not call them in the same process as `Server.start()`. Run two programs.

```bash
cd posix
make
./lux ../examples/ninep_status.lux
```

Second terminal, with [plan9port](https://9fans.github.io/plan9port/) `9p`:

```bash
9p -a unix!/tmp/lux.9p ls /
9p -a unix!/tmp/lux.9p read status
echo start | 9p -a unix!/tmp/lux.9p write ctl
```

Or the Lux client, no plan9port required:

```bash
./lux ../examples/ninep_client.lux unix!/tmp/lux.9p
```

TCP:

```bash
./lux ../examples/ninep_status.lux tcp!127.0.0.1!5640
./lux ../examples/ninep_client.lux tcp!127.0.0.1!5640
```

`post("lux")` on POSIX is `listen("/tmp/lux.9p")`.

## Serve from Plan 9

```lux
var fs = NineP();
fs.file("/status", readStatus, nil);
fs.post("lux");    # /srv/lux
```

Or TCP so a POSIX machine can dial in:

```lux
fs.listen("tcp!*!5640");
```

On Plan 9:

```
mount -c /srv/lux /n/lux
cat /n/lux/status
echo start > /n/lux/ctl
```

## Client: POSIX Lux talking to Plan 9 (or to Lux)

```lux
var c = NineP.connect("tcp!plan9.example!564");
if (c == nil) {
    print "connect failed";
    exit(1);
}

var names = c.ls("/");
print c.read("/LICENSE");
print c.stat("/LICENSE").length;
c.write("/tmp/from-posix", "hello\n");   // if the remote tree allows it
c.close();
```

`NineP.connect(addr)` returns a `NinePConn` or `nil`. Failed operations return `nil` / `false` and set `c.err`.

| Method | Result |
|---|---|
| `c.ls(path)` | array of names, or `nil` |
| `c.read(path)` | file bytes as a string, or `nil` (directories fail) |
| `c.write(path, data)` | `true` / `false` (writes from offset 0) |
| `c.get(remote, local)` | copy remote 9P file to a local path |
| `c.put(local, remote)` | copy a local file onto an existing remote 9P file |
| `c.stat(path)` | instance: `name`, `type` (`"file"` / `"dir"`), `length`, `mode`, `uid`, `gid` |
| `c.close()` | `true` — also run by GC |

Reads stop at 8 MiB. Walks are at most 16 path elements.

Connect to the example server:

```bash
./lux ../examples/ninep_client.lux
```

A mutable note file:

```bash
./lux ../examples/ninep_note.lux                 # terminal 1
echo remembered | 9p -a unix!/tmp/lux-note.9p write note
9p -a unix!/tmp/lux-note.9p read note
```

## Copying files

The synthetic server is still a flat list, not a full disk. You can still move **some** files — that is what `export` / `get` / `put` are for.

**POSIX serves a few files**, Plan 9 or another Lux copies them:

```lux
var fs = NineP();
fs.export("/README.md", "README.md");
fs.export("/note.txt", "/tmp/note.txt");
fs.listen("tcp!*!5640");
```

```lux
var c = NineP.connect("tcp!posixhost!5640");
c.get("/README.md", "/tmp/README.from9p");   // remote → here
c.put("/tmp/hello.txt", "/note.txt");        // here → remote (file must exist)
c.close();
```

```bash
cd posix
./lux ../examples/ninep_files.lux          # terminal 1
./lux ../examples/ninep_copy.lux           # terminal 2
```

**POSIX Lux talking to a Plan 9 export** (file must already exist for `put`; there is no create yet):

```lux
var c = NineP.connect("tcp!plan9!564");
c.get("/usr/glenda/readme", "readme.local");
c.put("local.txt", "/tmp/from-posix");      // /tmp/from-posix must exist on Plan 9
```

Reads and exports stop at 8 MiB. That is enough for source, notes, and configs; it is not a bulk disk copy.

## Plan 9 machine mounting POSIX Lux

On POSIX (unprivileged port):

```bash
./lux examples/ninep_status.lux tcp!*!5640
```

On Plan 9 / 9front:

```
srv tcp!posixhost!5640 luxposix
mount -c /srv/luxposix /n/lux
cat /n/lux/status
```

Firewall and bind address matter. `tcp!127.0.0.1!5640` is only the POSIX host itself. `tcp!*!5640` listens on every interface.

## Protocol tests (no Lux)

```bash
cd posix
make ninep_test
./ninep_test
```

That round-trips the server and client over a socketpair, a unix socket, and TCP localhost.

## Limits

- 9P2000 only: version, attach, walk, open, read, write, clunk, stat, flush
- no auth, create, remove, wstat
- synthetic server: flat `/`, 32 files, 8 KiB messages
- `listen` / `post` are blocking; one client at a time
- client `write` starts at offset 0 (good for ctl files)
- no TLS; put `tlssrv` / `tlsclient` in front if you need encryption

## Examples

- `examples/ninep_status.lux` — `/status` and `/ctl`
- `examples/ninep_client.lux` — connect, ls, read, write, stat
- `examples/ninep_note.lux` — last write is the next read
- `examples/ninep_files.lux` — export real files
- `examples/ninep_copy.lux` — `get` / `put` copies
