import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

REPEATS = 9
ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "benchmark"


def run_cmd(cmd: List[str], cwd: Optional[Path] = None) -> str:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd or ROOT),
        capture_output=True,
        text=True,
        check=True,
    )
    return proc.stdout


def parse_time(output: str) -> float:
    m = re.search(r"^BENCH_TIME=([0-9eE+\-.]+)$", output, flags=re.MULTILINE)
    if not m:
        raise ValueError(f"Missing BENCH_TIME in output:\n{output}")
    return float(m.group(1))


def benchmark(label: str, cmd: List[str], cwd: Optional[Path] = None) -> Dict[str, float]:
    samples: List[float] = []
    for _ in range(REPEATS):
        out = run_cmd(cmd, cwd=cwd)
        samples.append(parse_time(out))
    return {
        "label": label,
        "min": min(samples),
        "median": statistics.median(samples),
        "max": max(samples),
    }


def main() -> int:
    run_cmd(["cc", "-O3", str(BENCH / "fib_bench.c"), "-o", str(BENCH / "fib_bench_c")])
    run_cmd(["make", "-s"], cwd=ROOT / "posix")

    results = [
        benchmark("Python", [sys.executable, str(BENCH / "fib_bench.py")]),
        benchmark("POSIX C", [str(BENCH / "fib_bench_c")]),
        benchmark("Lux POSIX", [str(ROOT / "posix" / "my_program"), str(BENCH / "fib_bench.lux")]),
    ]

    print(f"Repeats per implementation: {REPEATS}")
    print("| Implementation | Min (s) | Median (s) | Max (s) |")
    print("|---|---:|---:|---:|")
    for row in results:
        print(
            f"| {row['label']} | {row['min']:.9f} | {row['median']:.9f} | {row['max']:.9f} |"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
