#!/usr/bin/env python3
"""
Compare Lux LoadTest vs k6 vs Locust on the same local HTTP target.

Usage (from repo root or this directory):
  python3 benchmark/load_compare/run_compare.py
  python3 benchmark/load_compare/run_compare.py --users 10 --duration 10s

Skips tools that are not installed. Writes out/compare.json and prints a table.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
POSIX = ROOT / "posix"
OUT = HERE / "out"


def which(name: str) -> Optional[str]:
    return shutil.which(name)


def parse_duration_seconds(s: str) -> int:
    s = s.strip().lower()
    if s.endswith("s"):
        return max(1, int(float(s[:-1])))
    if s.endswith("m"):
        return max(1, int(float(s[:-1]) * 60))
    return max(1, int(float(s)))


def wait_for_target(url: str, timeout: float = 10.0) -> None:
    import urllib.request

    deadline = time.time() + timeout
    last_err: Optional[Exception] = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url + "/health", timeout=1) as resp:
                if 200 <= resp.status < 300:
                    return
        except Exception as e:  # noqa: BLE001
            last_err = e
            time.sleep(0.1)
    raise RuntimeError(f"target not ready at {url}: {last_err}")


def start_target(host: str, port: int) -> subprocess.Popen:
    proc = subprocess.Popen(
        [sys.executable, str(HERE / "target.py"), "--host", host, "--port", str(port)],
        cwd=str(HERE),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return proc


def stop_proc(proc: Optional[subprocess.Popen]) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def run_lux(env: Dict[str, str], lux_bin: Path) -> Dict[str, Any]:
    out_dir = OUT / "lux"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    e = dict(os.environ)
    e.update(env)
    e["LOAD_OUT"] = str(out_dir)
    e["LUX_BIN"] = str(lux_bin)
    # import paths in scenario.lux are relative to cwd (SPEC): stay in this dir.
    cmd = [str(lux_bin), str(HERE / "scenario.lux")]
    subprocess.run(cmd, cwd=str(HERE), env=e, check=True)
    results_path = out_dir / "results.json"
    data = json.loads(results_path.read_text())
    return {
        "tool": "lux",
        "available": True,
        "count": data.get("count"),
        "failed": data.get("failed"),
        "rps": data.get("rps"),
        "avg_ms": data.get("avg"),
        "p50_ms": data.get("p50"),
        "p95_ms": data.get("p95"),
        "p99_ms": data.get("p99"),
        "bytes": data.get("bytes"),
        "raw": data,
        "dir": str(out_dir),
    }


def run_k6(env: Dict[str, str], duration: str, users: int) -> Dict[str, Any]:
    if not which("k6"):
        return {"tool": "k6", "available": False, "error": "k6 not on PATH"}
    out_dir = OUT / "k6"
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = out_dir / "summary.json"
    e = dict(os.environ)
    e.update(env)
    e["LOAD_VUS"] = str(users)
    e["LOAD_DURATION"] = duration
    cmd = [
        "k6",
        "run",
        "--quiet",
        "--summary-export",
        str(summary),
        str(HERE / "scenario.js"),
    ]
    subprocess.run(cmd, cwd=str(HERE), env=e, check=True)
    data = json.loads(summary.read_text())
    metrics = data.get("metrics", {})

    def mval(name: str, key: str) -> Optional[float]:
        block = metrics.get(name) or {}
        values = block.get("values") or {}
        if key in values:
            return values[key]
        # k6 v0.47+ summary-export often flattens into the metric object
        if key in block and isinstance(block[key], (int, float)):
            return float(block[key])
        if key == "rate" and isinstance(block.get("value"), (int, float)):
            return float(block["value"])
        return None

    failed = mval("http_req_failed", "rate")
    count = mval("http_reqs", "count")
    # duration of test in seconds for rps fallback
    dur_s = parse_duration_seconds(duration)
    avg = mval("http_req_duration", "avg")
    p50 = mval("http_req_duration", "med")
    if p50 is None:
        p50 = mval("http_req_duration", "p(50)")
    p95 = mval("http_req_duration", "p(95)")
    p99 = mval("http_req_duration", "p(99)")
    rps = mval("http_reqs", "rate")
    if rps is None and count is not None and dur_s > 0:
        rps = float(count) / dur_s
    return {
        "tool": "k6",
        "available": True,
        "count": count,
        "failed": failed,
        "rps": rps,
        "avg_ms": avg,
        "p50_ms": p50,
        "p95_ms": p95,
        "p99_ms": p99,
        "bytes": None,
        "raw": {"metrics_keys": list(metrics.keys())},
        "dir": str(out_dir),
    }


def run_locust(env: Dict[str, str], duration: str, users: int, spawn: int) -> Dict[str, Any]:
    if not which("locust"):
        return {"tool": "locust", "available": False, "error": "locust not on PATH"}
    out_dir = OUT / "locust"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    prefix = out_dir / "stats"
    e = dict(os.environ)
    e.update(env)
    host = e.get("LOAD_HOST", "http://127.0.0.1:8765")
    # Locust --run-time accepts 10s / 1m
    cmd = [
        "locust",
        "-f",
        str(HERE / "locustfile.py"),
        "--headless",
        "-u",
        str(users),
        "-r",
        str(spawn),
        "-t",
        duration,
        "--host",
        host,
        "--csv",
        str(prefix),
        "--skip-log-setup",
    ]
    subprocess.run(cmd, cwd=str(HERE), env=e, check=True)
    stats_path = Path(str(prefix) + "_stats.csv")
    # Locust CSV: Type,Name,Request Count,Failure Count,Median Response Time,...
    # Last data row often Aggregated
    count = failed_n = 0
    avg = p50 = p95 = p99 = None
    if stats_path.exists():
        import csv

        with stats_path.open() as f:
            rows = list(csv.DictReader(f))
        agg = None
        for row in rows:
            if row.get("Name") == "Aggregated" or row.get("Type") == "Aggregated":
                agg = row
        if agg is None and rows:
            agg = rows[-1]
        if agg:
            count = int(float(agg.get("Request Count") or 0))
            failed_n = int(float(agg.get("Failure Count") or 0))
            avg = float(agg.get("Average Response Time") or 0)
            p50 = float(agg.get("Median Response Time") or 0)
            # Locust columns vary by version; try common percentile names
            for key in ("95%", "95%ile", "P95", "p95"):
                if key in agg and agg[key] not in (None, ""):
                    p95 = float(agg[key])
                    break
            for key in ("99%", "99%ile", "P99", "p99"):
                if key in agg and agg[key] not in (None, ""):
                    p99 = float(agg[key])
                    break
    dur_s = parse_duration_seconds(duration)
    failed = (failed_n / count) if count else None
    rps = (count / dur_s) if dur_s > 0 else None
    return {
        "tool": "locust",
        "available": True,
        "count": count,
        "failed": failed,
        "rps": rps,
        "avg_ms": avg,
        "p50_ms": p50,
        "p95_ms": p95,
        "p99_ms": p99,
        "bytes": None,
        "raw": {"stats_csv": str(stats_path)},
        "dir": str(out_dir),
    }


def fmt(v: Any, digits: int = 2) -> str:
    if v is None:
        return "—"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def print_table(rows: List[Dict[str, Any]]) -> None:
    print()
    print("| Tool | Reqs | Fail | RPS | avg ms | p50 | p95 | p99 |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        if not r.get("available"):
            print(f"| {r['tool']} | skipped ({r.get('error', 'n/a')}) | | | | | | |")
            continue
        print(
            f"| {r['tool']} | {fmt(r.get('count'), 0)} | {fmt(r.get('failed'), 4)} | "
            f"{fmt(r.get('rps'))} | {fmt(r.get('avg_ms'))} | {fmt(r.get('p50_ms'))} | "
            f"{fmt(r.get('p95_ms'))} | {fmt(r.get('p99_ms'))} |"
        )
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--users", type=int, default=10)
    ap.add_argument("--duration", default="10s")
    ap.add_argument("--spawn-rate", type=int, default=0, help="Locust spawn/s (0 => =users)")
    ap.add_argument(
        "--wait",
        type=float,
        default=0.05,
        help="Think time seconds between requests for all tools (0 = hammer)",
    )
    ap.add_argument("--skip-lux", action="store_true")
    ap.add_argument("--skip-k6", action="store_true")
    ap.add_argument("--skip-locust", action="store_true")
    args = ap.parse_args()

    users = max(1, args.users)
    spawn = args.spawn_rate if args.spawn_rate > 0 else users
    base = f"http://{args.host}:{args.port}"
    env = {
        "LOAD_HOST": base,
        "LOAD_VUS": str(users),
        "LOAD_DURATION": args.duration,
        "LOAD_WAIT": str(max(0.0, args.wait)),
    }

    lux_bin = POSIX / "lux"
    if not lux_bin.is_file() and not args.skip_lux:
        print(f"Building {lux_bin} ...", flush=True)
        subprocess.run(["make", "-s"], cwd=str(POSIX), check=True)

    OUT.mkdir(parents=True, exist_ok=True)
    target = None
    rows: List[Dict[str, Any]] = []
    try:
        print(f"Starting target on {base} ...", flush=True)
        target = start_target(args.host, args.port)
        wait_for_target(base)
        print(
            f"Scenario: users={users} duration={args.duration} wait={args.wait}s "
            f"mix=GET/hello×5 POST/echo×1 POST/graphql×1",
            flush=True,
        )

        if not args.skip_lux:
            print("Running Lux LoadTest ...", flush=True)
            try:
                rows.append(run_lux(env, lux_bin))
            except Exception as e:  # noqa: BLE001
                rows.append({"tool": "lux", "available": False, "error": str(e)})

        if not args.skip_k6:
            print("Running k6 ...", flush=True)
            try:
                rows.append(run_k6(env, args.duration, users))
            except Exception as e:  # noqa: BLE001
                rows.append({"tool": "k6", "available": False, "error": str(e)})

        if not args.skip_locust:
            print("Running Locust ...", flush=True)
            try:
                rows.append(run_locust(env, args.duration, users, spawn))
            except Exception as e:  # noqa: BLE001
                rows.append({"tool": "locust", "available": False, "error": str(e)})

        print_table(rows)
        compare_path = OUT / "compare.json"
        compare_path.write_text(
            json.dumps(
                {
                    "host": base,
                    "users": users,
                    "duration": args.duration,
                    "results": rows,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"Wrote {compare_path}")
        return 0
    finally:
        stop_proc(target)


if __name__ == "__main__":
    raise SystemExit(main())
