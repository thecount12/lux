# Load compare results

Recorded runs of Lux `LoadTest` vs k6 vs Locust. Raw artifacts live under `out/`.

## 1. Local toy target (client comparison)

**When:** 2026-11-17  
**Target:** `http://127.0.0.1:8765` (`target.py` — trivial JSON)  
**Settings:** 5 users · 5s · 50ms think · mix GET `/hello` ×5, POST `/echo` ×1, POST `/graphql` ×1  

| Tool | Reqs | Fail | RPS | avg ms | p50 | p95 | p99 |
|------|-----:|-----:|----:|-------:|----:|----:|----:|
| lux | 412 | 0 | 82.4 | 0.95 | 0.90 | 1.62 | 2.07 |
| k6 | 480 | 0 | 95.5 | 1.47 | 1.42 | 2.66 | 2.98 |
| locust | 350 | 0 | 70.0 | 3.05 | 3.00 | 5.00 | 6.00 |

**Takeaway:** On a local no-op server, Lux landed between Locust and k6 on RPS and looked strong on latency. This measures **load generators**, not a real site.

Reproduce:

```bash
source ~/.venv/locust/bin/activate
export PATH="/opt/homebrew/bin:$PATH"
python3 benchmark/load_compare/run_compare.py --users 5 --duration 5s
```

---

## 2. Tumeryk homepage (real HTTPS site)

**Target:** `https://www.tumeryk.com/` (homepage GET only — no API)  
**Settings:** 5 users · 10s · think 0.5–1.0s · identifiable `User-Agent`  
**Note:** Run authorized by a site co-builder for Lux tooling comparison. Keep future runs polite.

### Run B (confirm) — 2026-11-18 after closing other apps

**Probe:** status 200 · ~400 ms · 9226 bytes  

| Tool | Reqs | Fail | RPS | avg ms | p50 | p95 | p99 |
|------|-----:|-----:|----:|-------:|----:|----:|----:|
| lux | 57 | 0 | 5.7 | 103.0 | 94.4 | 149.9 | 206.1 |
| k6 | 66 | 0 | 6.2 | 49.0 | 44.4 | 79.0 | 108.1 |
| locust | 53 | 0 | 6.0 | 56.5 | 52.0 | 96.0 | 100.0 |

### Run A — 2026-11-17

**Probe:** status 200 · ~238 ms · 9226 bytes  

| Tool | Reqs | Fail | RPS | avg ms | p50 | p95 | p99 |
|------|-----:|-----:|----:|-------:|----:|----:|----:|
| lux | 59 | 0 | 5.9 | 102.1 | 93.4 | 136.6 | 180.2 |
| k6 | 65 | 0 | 5.9 | 64.7 | 60.2 | 104.0 | 173.3 |
| locust | 51 | 0 | 5.1 | 57.6 | 52.0 | 97.0 | 120.0 |

**Artifacts:**

- `out/tumeryk/lux/results.txt` · `results.json` · `run.log` · `samples-all.txt`
- `out/tumeryk/k6/summary.json`
- `out/tumeryk/locust/stats_stats.csv`
- `out/tumeryk/compare.json`

**Takeaway (before keep-alive):** Both runs agreed. With polite think time, all three stayed near ~5–6 RPS and **0% fail**. Lux was reliable but slower per request (fresh TCP/TLS every `httpStat`).

### Run C — after POSIX `httpStat` CURL keep-alive (2026-11-18)

Same settings as Run B. Lux workers now reuse one CURL easy handle per process.

| Tool | Reqs | Fail | RPS | avg ms | p50 | p95 | p99 |
|------|-----:|-----:|----:|-------:|----:|----:|----:|
| lux | 62 | 0 | 6.2 | 58.3 | 53.5 | 79.4 | 116.2 |
| k6 | 63 | 0 | 5.8 | 57.2 | 50.6 | 86.5 | 139.1 |

**Takeaway:** Keep-alive closed the gap. Lux avg/p95 now match k6 on Tumeryk (~58 ms / ~79 ms vs prior Lux ~103 / ~150). Artifacts: `out/tumeryk-reuse/`.

Reproduce:

```bash
source ~/.venv/locust/bin/activate
export PATH="/opt/homebrew/bin:$PATH"
ROOT=.../lux
CMP=$ROOT/benchmark/load_compare
LUX=$ROOT/posix/lux
HOST=https://www.tumeryk.com

cd "$CMP"
LOAD_HOST=$HOST LOAD_VUS=5 LOAD_DURATION=10s LOAD_WAIT=0.5 \
  LOAD_OUT=$CMP/out/tumeryk/lux LUX_BIN=$LUX \
  $LUX scenario_tumeryk.lux

LOAD_HOST=$HOST LOAD_VUS=5 LOAD_DURATION=10s LOAD_WAIT=0.5 \
  k6 run --summary-export $CMP/out/tumeryk/k6/summary.json scenario_tumeryk.js

LOAD_HOST=$HOST LOAD_WAIT=0.5 \
  locust -f locustfile_tumeryk.py --headless -u 5 -r 2 -t 10s \
  --host $HOST --csv $CMP/out/tumeryk/locust/stats
```

Do not raise users/duration against production without a clear plan.
