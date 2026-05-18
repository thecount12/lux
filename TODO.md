# Lux — HTTP server / middleware TODO

Tracked work for the `Server` class and `server.use()` middleware. Check in and pick up later.

## Logger / middleware crash (Plan 9)

**Symptom:** After a successful HTTP response, the interpreter dies with `suicide: fault read addr=0x0`, often near `serverStartNative` → `close(dfd)` in `vm.c`. Acid stack shows corrupted VM state after nested `run()`.

**Repro:**
- `server.start()` with a route handler and `server.use(logger)` where `logger` calls `next()`.
- `curl -H "Content-Type: application/json" -d '{"name":"John"}' http://host:8080/data`
- Same handler **without** `server.use(logger)` does not crash.

**Root cause (hypothesis):**
- `server.start()` is a native that blocks inside `invokeFromClass` while the main `run()` is still active; `invokeFromClass` does not pop the stack until the native returns.
- Each request calls `run()` again from C (`serverStartNative` / `resNextNative`).
- Middleware adds a **second** nested `run()` (`logger` → `next()` → handler), which corrupts `vm.stack` / frames.

**Files:** `vm.c`, `posix/vm.c` — `serverStartNative`, `resNextNative`, `invokeFromClass`.

### Tasks

- [ ] **Fix re-entrant `run()` from HTTP server**
  - Option A: At entry to `serverStartNative`, pop the suspended `server.start()` invoke slots off `vm.stack` (receiver + args); return a value only when the server loop exits.
  - Option B: Save/restore `vm.stackTop`, `vm.frameCount`, and `openUpvalues` around each request handler invocation.
  - Option C: Do not call `run()` from native — dispatch handler bytecode on the existing frame (same idea as `importModule` in `vm.c`).

- [ ] **Verify middleware path** after VM fix: `server.use(logger)` + `next()` + POST/GET routes on Plan 9 and POSIX.

- [ ] **Add regression test**
  - Minimal `.lux` server with one middleware and one POST route.
  - `luxtest` cannot curl; use a small C test or documented manual script, or fork server + `hget`/`curl` in `rc` with timeout (see `tests/post.rc`).

- [ ] **Re-enable README example** — uncomment `server.use(logger)` in README once fixed.

## `res.json` + `Dict` (documentation / optional feature)

- [ ] **Document** (done in README) that `res.json(dict)` serializes instance fields only, not native `Dict` entries.
- [ ] **Optional:** Teach `res.json` to detect `Dict` class and serialize via `dict_iter`, or skip `_ptr` in `serializeJsonObject`.

## README / examples alignment

- [x] Update README `Server` examples (`parseJSON`, no fake `{key: val}` literals, middleware warning).
- [ ] Align `examples/static_server.lux` comment if we add a `web.lux` example under `examples/`.

## 9front deployment (separate from VM bug)

- [ ] Document rc-httpd + redirect to Lux port 8080 vs Lux on :80 (TLS/`tlssrv` notes) if we add `docs/` or README section.
