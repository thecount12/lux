import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

REPEATS = 5
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


def parse_bench(output: str) -> Dict[str, float]:
    m_native = re.search(r"^BENCH_NATIVE=([0-9eE+\-.]+)$", output, flags=re.MULTILINE)
    m_lux = re.search(r"^BENCH_LUX=([0-9eE+\-.]+)$", output, flags=re.MULTILINE)
    if not m_native or not m_lux:
        raise ValueError(f"Missing BENCH_NATIVE or BENCH_LUX in output:\n{output}")
    return {"native": float(m_native.group(1)), "lux": float(m_lux.group(1))}


def benchmark(cmd: List[str]) -> Dict[str, Dict[str, float]]:
    native_samples: List[float] = []
    lux_samples: List[float] = []
    for _ in range(REPEATS):
        out = run_cmd(cmd)
        res = parse_bench(out)
        native_samples.append(res["native"])
        lux_samples.append(res["lux"])
    return {
        "native": {
            "min": min(native_samples),
            "median": statistics.median(native_samples),
            "max": max(native_samples),
        },
        "lux": {
            "min": min(lux_samples),
            "median": statistics.median(lux_samples),
            "max": max(lux_samples),
        },
    }


def main() -> int:
    # Build POSIX lux if needed
    run_cmd(["make", "-C", str(ROOT / "posix"), "-s"])

    cmd = [str(ROOT / "posix" / "lux"), str(BENCH / "float64_example.lux")]
    results = benchmark(cmd)

    print("Float64 dot benchmark (seconds)")
    print("| Impl | Min | Median | Max |")
    print("|---|---:|---:|---:|")
    print(f"| native | {results['native']['min']:.9f} | {results['native']['median']:.9f} | {results['native']['max']:.9f} |")
    print(f"| lux-loop | {results['lux']['min']:.9f} | {results['lux']['median']:.9f} | {results['lux']['max']:.9f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

