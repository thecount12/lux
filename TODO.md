# Lux — HTTP server / middleware TODO

Tracked work for the `Server` class and `server.use()` middleware.

## Logger / middleware crash (Plan 9) — fixed

**Was:** After a successful HTTP response with middleware, the interpreter died with `suicide: fault read addr=0x0` (nested `run()` continued into the suspended `server.start()` caller frame).

**Fix:** `run()` records `baseFrameCount` on entry and returns from `OP_RETURN` when the frame count drops back to that base (instead of only when it hits 0). HTTP dispatch / `res.next` also restore `vm.stackTop` after nested `run()`.

**Verify (manual):**
```sh
# POSIX
./lux examples/server_middleware.lux
curl http://127.0.0.1:8080/hello
curl -H "Content-Type: application/json" -d '{"name":"John"}' http://127.0.0.1:8080/data
```

- [x] Fix re-entrant `run()` from HTTP server
- [x] Re-enable README / `examples/server_middleware.lux` logger example
- [ ] Optional: automated regression (fork server + `curl`/`hget` with timeout; see `tests/post.rc`)

## `res.json` + `Dict` (documentation / optional feature)

- [x] **Document** (done in README) that `res.json(dict)` serializes instance fields only, not native `Dict` entries.
- [ ] **Optional:** Teach `res.json` to detect `Dict` class and serialize via `dict_iter`, or skip `_ptr` in `serializeJsonObject`.

## README / examples alignment

- [x] Update README `Server` examples (`parseJSON`, no fake `{key: val}` literals).
- [x] Middleware logger example enabled.
- [ ] Align `examples/static_server.lux` comment if we add a `web.lux` example under `examples/`.

## 9front deployment (separate)

- [x] Lux `Server` supports multi-domain via `req.host`, `server.vhost`, `getHost`/`postHost` (one process on `:8080`). See `examples/vhost_server.lux` and README Server section.
- Deployment sketch: `tcp443` → `tlssrv` (SNI/acmed) → Lux `:8080`; `tcp80` → `rc-httpd` for ACME + `www.*` → apex redirects; register only apex Host names in Lux.
