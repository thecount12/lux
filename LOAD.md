# Lux load and performance testing

Locust-style HTTP load tests ship with Lux: the `lux` binary plus `lib/load.lux`. No k6, Locust, or Python install. Point the library at any website you are allowed to test.

Related: interpreter microbenchmarks live in [`benchmark.md`](benchmark.md). k6 / curl / Playwright wrappers live in [`lib/webtest.lux`](lib/webtest.lux).

## Quick start

From `posix/` (so workers can exec `./lux`):

```bash
cd posix
./lux ../examples/load_demo.lux
```

Minimal script:

```lux
import "../lib/load.lux";

var t = LoadTest();
t.bin("./lux");
t.outDir("load-out-" + epoch());   // keep artifacts next to you
t.users(5);
t.spawnRate(2);
t.duration("20s");
t.wait(0.5, 1.5);
t.header("User-Agent", "lux-load/1.0 (+https://luxlang.dev)");
t.get("https://example.com/");
t.task(3, "GET", "https://example.com/pricing", nil);
t.post("https://example.com/api", "{\"n\":1}");

var s = t.run();
print "dir=" + s.dir;
print "reqs=" + s.count + " fail=" + s.failed + " p95=" + s.p95;
```

## How it works

1. You register a **task menu** with `get` / `post` / `task` (no HTTP yet).
2. `run()` creates an output directory, writes worker scripts and config JSON.
3. Each virtual user is a **Lux process** that loops until the duration ends: pick a weighted task → `httpStat` → append a sample line → optional `sleep`.
4. The parent merges samples and writes **results** and a **run log**.

Caps: **1–128** users (soft). One OS process per user — **~100 VUs** is a solid work max on a typical task/container; past that prefer `K6` in `lib/webtest.lux`.

Natives used:

| Native | Role |
|--------|------|
| `httpStat(method, url, body, headers)` | Timed request; returns `status`, `ms`, `bytes`, `error`, `reused`; **discards body** |
| `sleep(seconds)` | Think time between requests |

On POSIX, `httpStat` reuses one libcurl easy handle per process so TCP/TLS can stay warm across calls in a load worker (`reused` is true when no new connect was made). Plan 9 still dials per request.

## Building the scenario

### `get`, `post`, `task`

These only **register** requests. They do not hit the network.

| Call | Meaning |
|------|---------|
| `t.get(url)` | weight 1, `GET`, no body |
| `t.post(url, body)` | weight 1, `POST`, string body |
| `t.task(weight, method, url, body)` | full control |

**Weight** is relative frequency (Locust-style). Example:

```lux
t.task(5, "GET", "https://example.com/", nil);        // ~5/7
t.task(1, "GET", "https://example.com/pricing", nil);  // ~1/7
t.post("https://example.com/api", "{\"n\":1}");        // ~1/7
```

Every loop iteration picks one task at random by weight until the clock runs out. It is not “run get once, then post once.”

### Users, duration, think time

```lux
t.users(10);          // virtual users (clamped 1..128)
t.spawnRate(2);       // new users per second when users > 1
t.duration(30);       // seconds, or "30s" / "2m"
t.wait(1, 3);         // sleep between requests (seconds, inclusive range)
t.bin("./lux");       // lux binary workers exec (default "lux")
t.outDir("load-out"); // where logs and results go
t.header("Name", "value");
```

Be polite on public sites: few users, short duration, identifiable `User-Agent`.

## Output files

By default artifacts go under `/tmp/lux-load-<epoch>-N/` (easy to miss). Set a local directory:

```lux
t.outDir("load-out-" + epoch());
```

After `t.run()`, that directory contains:

| File | Contents |
|------|----------|
| `results.txt` | Human-readable summary |
| `results.json` | Same metrics as JSON (for scripts / CI) |
| `run.log` | Timestamped start / worker / finish lines (`epoch` seconds) |
| `samples-0.txt` … | Per-user raw lines: `status ms bytes` |
| `samples-all.txt` | Concatenation of all sample files |
| `cfg-N.json` | Per-user worker config |
| `worker-N.lux` | Generated worker program |
| `run.sh` | Multi-user launcher (when `users > 1`) |

`LoadResult` fields (also on `results.json`):

| Field | Meaning |
|-------|---------|
| `ok` | Parsed at least one sample |
| `count` | Total requests |
| `failed` | Fraction with status outside 2xx (includes `0` transport failures) |
| `rps` | `count / durationSec` |
| `avg`, `p50`, `p95`, `p99` | Latency in **milliseconds** |
| `bytes` | Sum of response body sizes |
| `dir` | Output directory path |
| `error` | Set when `ok` is false |

Example `results.txt`:

```text
Lux load test results
dir: load-out-1730000000
users: 5
durationSec: 20
...
count: 84
failed: 0
rps: 4.2
p95_ms: 87.4
```

## `workerScript` (optional)

`t.workerScript(cfgPath, outPath)` returns the Lux source one worker would run. `run()` writes that string to `worker-N.lux` for you. Print it only when debugging:

```lux
print t.workerScript(t.dir + "/cfg-0.json", t.dir + "/samples-0.txt");
```

## Asserting in tests

```lux
var s = t.run();
assert(s.ok == true, "ran");
assert(s.failed < 0.01, "error rate");
assert(s.p95 < 500, "p95 ms");
assert(fileExists(s.dir + "/results.json"), "results file");
assert(fileExists(s.dir + "/run.log"), "log file");
```

Offline unit coverage (no network) lives in `tests/test_load.lux` (script generation + `parseSamples`). Live smoke runs only if `httpStat` can reach `example.com`.

## vs k6 / Locust / `benchmark.md`

| Tool | Use when |
|------|----------|
| `lib/load.lux` | Marketing demos, CI smoke, tens of users, **only Lux** on the machine |
| `lib/webtest.lux` `K6` | Hundreds of VUs, k6 already installed |
| `benchmark.md` / `benchmark/` | Lux **interpreter** speed (fib), not HTTP |

### Side-by-side harness

[`benchmark/load_compare/`](benchmark/load_compare/) runs the **same** weighted scenario (GET + POST + GraphQL POST) against a local `target.py` with Lux, k6, and Locust (skips whatever is not installed):

```bash
python3 benchmark/load_compare/run_compare.py
python3 benchmark/load_compare/run_compare.py --users 10 --duration 10s
```

See [`benchmark/load_compare/README.md`](benchmark/load_compare/README.md). Recorded numbers (localhost + a polite Tumeryk homepage run): [`benchmark/load_compare/RESULTS.md`](benchmark/load_compare/RESULTS.md).

## Plan 9 notes

`httpStat` and `sleep` exist on Plan 9. Multi-user spawning uses `sh` + background jobs (POSIX-oriented). Prefer `t.users(1)` on Plan 9, or generate workers and launch them with `rc` yourself. Script generation (`workerScript`, `configJson`) still works everywhere.

## Etiquette

- Only load-test systems you own or have permission to test.
- Start with `users(1)` and a few seconds; raise load gradually.
- Send a clear `User-Agent` that names Lux and a contact URL.
- Watch `failed` and `p95` together: a “fast” p95 with a high fail rate is not success.

## Shipping metrics to Datadog

ECS sidecars usually expose DogStatsD on `127.0.0.1:8125` (or `DD_AGENT_HOST`).

```lux
import "../lib/statsd.lux";
import "../lib/load.lux";

var dd = StatsD();
dd.fromEnv();                 // DD_AGENT_HOST / DD_DOGSTATSD_PORT
dd.prefix("lux.load.");
dd.tag("service", "my-api");
dd.tag("env", "prod");

var t = LoadTest();
// ... configure and run ...
var s = t.run();
dd.loadResult(s, ["site:tumeryk"]);
```

Or raw: `statsdSend("lux.load.requests:1|c|#service:api")`. See `examples/statsd_demo.lux`.
