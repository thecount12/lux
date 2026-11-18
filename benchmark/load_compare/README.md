# Load compare: Lux vs k6 vs Locust

Same HTTP scenario, same local target, side-by-side metrics.

## What it runs

| Piece | Role |
|-------|------|
| `target.py` | Threaded local server: `GET /hello`, `POST /echo`, `POST /graphql` |
| `scenario.lux` | Lux `LoadTest` (weights 5 / 1 / 1) |
| `scenario.js` | k6 equivalent |
| `locustfile.py` | Locust equivalent |
| `run_compare.py` | Starts target, runs available tools, prints a table |

Default knobs: **10 users**, **10s**, **50ms think time** (shared via `LOAD_WAIT`). Mix: 5× GET `/hello`, 1× POST `/echo`, 1× POST `/graphql`. Use `--wait 0` to hammer.

Recorded result tables: [`RESULTS.md`](RESULTS.md).

## Run

```bash
# optional clients
brew install k6
pip install locust   # or: source ~/.venv/locust/bin/activate

python3 benchmark/load_compare/run_compare.py
python3 benchmark/load_compare/run_compare.py --users 20 --duration 15s
python3 benchmark/load_compare/run_compare.py --skip-locust
```

Polite homepage-only scripts for an external site (example: Tumeryk): `scenario_tumeryk.lux`, `scenario_tumeryk.js`, `locustfile_tumeryk.py`.

Artifacts:

- `out/lux/results.json` — Lux LoadTest summary  
- `out/k6/summary.json` — k6 summary export  
- `out/locust/*_stats.csv` — Locust CSV  
- `out/compare.json` — combined table data  

Missing tools are skipped (not a hard failure).

## How to read the table

- **RPS / p95** — higher RPS and lower p95 usually mean a more efficient client at this concurrency.
- **Fail** — fraction of non-2xx (Lux counts transport errors as status `0`). If Lux fail rate spikes while k6 stays clean, the local accept queue or file-descriptor limits are often the cause—not the site under test.
- Lux uses **one OS process per VU**; k6 and Locust multiplex VUs in-process. At 10 users Lux is competitive for demos; at high VU counts expect k6/Locust to pull ahead on the *client* side.
- The target is local and trivial on purpose so you compare **load generators**, not `example.com`.
- Default think time is **0** so throughput is comparable; that is harder on the machine than a polite public-site demo.

## Manual one-tool runs

```bash
# terminal 1
python3 benchmark/load_compare/target.py --port 8765

# Lux (cwd = this directory so import resolves)
cd benchmark/load_compare
LOAD_HOST=http://127.0.0.1:8765 LOAD_VUS=10 LOAD_DURATION=10s \
  LOAD_OUT=out/lux LUX_BIN=../../posix/lux \
  ../../posix/lux scenario.lux

# k6
LOAD_HOST=http://127.0.0.1:8765 k6 run scenario.js

# Locust
locust -f locustfile.py --headless -u 10 -r 10 -t 10s \
  --host http://127.0.0.1:8765
```
