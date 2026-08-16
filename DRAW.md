# Draw, snarf, plumber, and 9P

Lux exposes a Plan 9-shaped I/O GUI: clipboard, a single draw window, plumber messages, and a synthetic 9P tree. There is no widget toolkit.

Do not multiplex `Draw.event()`, `NineP.listen()`, and `Server.start()` in one process. Run them as separate programs and connect them with plumber or 9P.

## Build

Plan 9 (always linked with libdraw / libevent / libplumb):

```
mk
```

POSIX, without a window (snarf via `pbcopy`/`pbpaste` or `xclip`; 9P still works):

```
cd posix
make
```

Draw and plumber on macOS/Linux/OpenBSD need [plan9port](https://9fans.github.io/plan9port/) (`libdraw`, `devdraw`, `plumber`, and the `9c`/`9l` compilers). You are not installing Plan 9 the OS. Snarf and 9P do not require it.

### macOS: install plan9port

Homebrew:

```
brew install plan9port
```

Or build from source (`git clone https://github.com/9fans/plan9port` then `./INSTALL`). At the end of `install.log` it prints the `PLAN9` path to put in your profile — that directory, not `/usr/local/plan9`, unless you installed there.

Add to `~/.zshrc`. Use the path `INSTALL` printed (Homebrew is `$(brew --prefix)/plan9`). In zsh use `export PLAN9=...`, not the `PLAN9=... export PLAN9` line `INSTALL` prints (that form leaves `$PLAN9` empty in zsh):

```
export PLAN9=/Users/you/codechanges/plan9port   # or: $(brew --prefix)/plan9
export PATH="$PATH:$PLAN9/bin"
```

Open a new terminal (or `source ~/.zshrc`) and check:

```
echo $PLAN9
which 9c 9l plumber
```

Then:

```
cd posix
make USE_P9P=1 PLAN9="$PLAN9"
./lux ../examples/draw_hello.lux
```

That builds helper `luxp9` (the only file that includes plan9port headers). Override its path with `LUXP9` if it is not next to `./lux`. Click to draw dots; press `q` to quit.

Other Draw examples:

```
./lux ../examples/draw_file.lux ../DRAW.md   # file viewer (j/k, space/b, q)
./lux ../examples/draw_dir.lux .             # directory list (j/k, enter snarf)
./lux ../examples/draw_type.lux              # type into the window (Esc quits)
```

Start plumber once per login before `plumb` / `plumbRecv`:

```
plumber
./lux ../examples/plumb_send.lux
```

If `Draw()` fails, `9c` is missing from `PATH`, or `PLAN9` does not match the install.

## API

```
print draw_available();     // true if this build can open a window

snarfPut("hello");
print snarfGet();

var w = Draw("lux", 640, 480);   // one window per process
w.fill(0, 0, w.width, w.height, "#111111");
w.string(8, 4, "hello", "#eeeeee");
w.flush();
var e = w.event();               // kind, x, y, button, r
w.close();                       // native resources are not GC'd

plumb("edit", "file.lux:42");    // optional third arg: wdir
var msg = plumbRecv();           // src, dst, wdir, type, data

var fs = NineP();
fs.file("/status", readStatus, nil);
fs.file("/ctl", nil, writeCtl);
fs.listen("/tmp/lux.9p");        // POSIX unix socket (mode 0600)
fs.post("lux");                  // Plan 9 /srv/lux; POSIX /tmp/lux.9p
```

Event `kind`: `"mouse"`, `"kbd"`, `"resize"`, `"quit"`. Mouse `button` is the Plan 9 bitfield (1=left, 2=middle, 4=right); motion arrives with `button == 0`. `r` is a Unicode codepoint. Color is `"#RRGGBB"`. Default font only. After `Draw()`, `w.width` / `w.height` are the actual window in pixels (on a Retina screen that may be larger than the size you asked for). `w.fontHeight` and `w.fontWidth` are the default font cell. `string(x, y, …)` treats `y` as the top of the line, not the Plan 9 baseline.

`Draw.init` returning `nil` still leaves a dead instance if construction fails after allocation; check `draw_available()` first and treat a failed open as unusable (`fill`/`event` return `nil`/`false`).

9P v1 is a synthetic tree: version, attach, walk, open, read, write, clunk, stat, flush. No auth, create, remove, or wstat.

## Examples

- `examples/snarf.lux`
- `examples/draw_hello.lux`
- `examples/draw_file.lux`
- `examples/draw_dir.lux`
- `examples/draw_type.lux`
- `examples/plumb_send.lux`
- `examples/ninep_status.lux`

POSIX 9P protocol test (no Lux, no display):

```
cd posix
make ninep_test
./ninep_test
```
