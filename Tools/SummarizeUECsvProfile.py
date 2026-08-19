#!/usr/bin/env python3
"""Summarize paired Unreal CSV profiler captures for the Kulla-Conty E_avg A/B test."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import re
import statistics
from collections import defaultdict
from pathlib import Path


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("Cannot calculate a percentile of an empty sample.")
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def read_unreal_csv(path: Path) -> tuple[list[str], list[list[str]], dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))

    header_index = -1
    for index in range(len(rows) - 1, -1, -1):
        if rows[index] and rows[index][0].strip().upper() == "EVENTS":
            header_index = index
            break
    if header_index < 0:
        raise ValueError(f"{path}: Unreal summary header row 'EVENTS' was not found.")

    headers = [cell.strip() for cell in rows[header_index]]
    data_rows = [row for row in rows[:header_index] if row and not row[0].startswith("[")]
    metadata: dict[str, str] = {}
    for row in rows[header_index + 1 :]:
        if len(row) >= 2 and row[0].startswith("[") and row[0].endswith("]"):
            metadata[row[0][1:-1]] = row[1]
    return headers, data_rows, metadata


def find_column(headers: list[str], kind: str) -> int | None:
    normalized = [header.replace(" ", "").lower() for header in headers]
    if kind == "gpu":
        preferred = (
            "gpu/unaccounted",
            "gpu/queuetotal",
            "gpu/gpuframetime",
            "gpu/frame",
        )
        for candidate in preferred:
            if candidate in normalized:
                return normalized.index(candidate)
        for index, name in enumerate(normalized):
            if name.startswith("gpu/") and (name.endswith("unaccounted") or name.endswith("queuetotal")):
                return index
        return None
    if kind == "frame":
        for candidate in ("frametime", "global/frametime"):
            if candidate in normalized:
                return normalized.index(candidate)
        for index, name in enumerate(normalized):
            if name.endswith("/frametime") and not name.startswith("gpu/"):
                return index
        return None
    raise ValueError(f"Unknown column kind: {kind}")


def numeric_column(rows: list[list[str]], index: int) -> list[float]:
    values: list[float] = []
    for row in rows:
        if index >= len(row) or not row[index].strip():
            continue
        try:
            value = float(row[index])
        except ValueError:
            continue
        if math.isfinite(value) and value > 0.0:
            values.append(value)
    return values


def trim(values: list[float], trim_start: int, trim_end: int) -> list[float]:
    stop = len(values) - trim_end if trim_end else len(values)
    result = values[trim_start:stop]
    if not result:
        raise ValueError(
            f"No samples remain after trimming {trim_start} start and {trim_end} end frames "
            f"from {len(values)} values."
        )
    return result


def describe(values: list[float]) -> dict[str, float | int]:
    return {
        "samples": len(values),
        "mean_ms": statistics.fmean(values),
        "median_ms": statistics.median(values),
        "p95_ms": percentile(values, 0.95),
        "min_ms": min(values),
        "max_ms": max(values),
    }


def infer_mode(path: Path, metadata: dict[str, str]) -> str:
    for key, value in metadata.items():
        if key.lower() == "kullacontymode":
            return value.strip().lower()
    lower_name = path.stem.lower()
    if "fast" in lower_name:
        return "fast"
    if "reference" in lower_name:
        return "reference"
    return "unknown"


def infer_trial(path: Path, metadata: dict[str, str]) -> int | None:
    for key, value in metadata.items():
        if key.lower() == "kullacontytrial":
            try:
                return int(value)
            except ValueError:
                break
    match = re.search(r"_(\d+)(?:_|$)", path.stem)
    return int(match.group(1)) if match else None


def bootstrap_mean_ci(
    values: list[float], resamples: int, seed: int = 0x4B554C4C
) -> tuple[float, float]:
    if not values:
        raise ValueError("Cannot bootstrap an empty sample.")
    if resamples <= 0:
        raise ValueError("Bootstrap resamples must be positive.")
    rng = random.Random(seed)
    count = len(values)
    estimates = [
        statistics.fmean(values[rng.randrange(count)] for _ in range(count))
        for _ in range(resamples)
    ]
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def build_paired_comparison(
    capture_medians: dict[str, dict[int, float]], bootstrap_resamples: int
) -> dict[str, object] | None:
    if "fast" not in capture_medians or "reference" not in capture_medians:
        return None
    paired_trials = sorted(
        set(capture_medians["fast"]).intersection(capture_medians["reference"])
    )
    if not paired_trials:
        return None

    pairs: list[dict[str, float | int]] = []
    deltas: list[float] = []
    speedups: list[float] = []
    for trial in paired_trials:
        fast = capture_medians["fast"][trial]
        reference = capture_medians["reference"][trial]
        delta = fast - reference
        speedup = (reference - fast) / reference * 100.0
        deltas.append(delta)
        speedups.append(speedup)
        pairs.append(
            {
                "trial": trial,
                "fast_median_ms": fast,
                "reference_median_ms": reference,
                "fast_minus_reference_ms": delta,
                "fast_speedup_percent": speedup,
            }
        )

    delta_ci = bootstrap_mean_ci(deltas, bootstrap_resamples)
    speedup_ci = bootstrap_mean_ci(speedups, bootstrap_resamples, seed=0x434F4E54)
    if len(paired_trials) < 3:
        conclusion = "insufficient_trials"
    elif delta_ci[1] < 0.0:
        conclusion = "fast_faster"
    elif delta_ci[0] > 0.0:
        conclusion = "reference_faster"
    else:
        conclusion = "inconclusive"

    return {
        "paired_trial_count": len(paired_trials),
        "pairs": pairs,
        "mean_fast_minus_reference_ms": statistics.fmean(deltas),
        "median_fast_minus_reference_ms": statistics.median(deltas),
        "mean_delta_95ci_ms": list(delta_ci),
        "mean_fast_speedup_percent": statistics.fmean(speedups),
        "mean_speedup_95ci_percent": list(speedup_ci),
        "conclusion": conclusion,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path, help="UE CSV files or directories containing them")
    parser.add_argument("--trim-start", type=int, default=120)
    parser.add_argument("--trim-end", type=int, default=30)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--require-pair", action="store_true")
    parser.add_argument("--bootstrap-resamples", type=int, default=20000)
    args = parser.parse_args()

    csv_paths: list[Path] = []
    generated_csv_names = {"summary.csv", "paired_trials.csv"}
    for path in args.paths:
        if path.is_dir():
            csv_paths.extend(
                sorted(
                    candidate
                    for candidate in path.rglob("*.csv")
                    if candidate.name.lower() not in generated_csv_names
                )
            )
        elif path.suffix.lower() == ".csv" and path.name.lower() not in generated_csv_names:
            csv_paths.append(path)
    csv_paths = sorted(set(path.resolve() for path in csv_paths))
    if not csv_paths:
        parser.error("No CSV captures were found.")

    captures: list[dict[str, object]] = []
    per_mode_gpu: dict[str, list[float]] = defaultdict(list)
    capture_medians: dict[str, dict[int, float]] = defaultdict(dict)
    for path in csv_paths:
        headers, rows, metadata = read_unreal_csv(path)
        gpu_index = find_column(headers, "gpu")
        if gpu_index is None:
            gpu_headers = [header for header in headers if header.lower().startswith("gpu/")]
            raise ValueError(
                f"{path}: no whole-frame GPU column was found. Available GPU columns: "
                + ", ".join(gpu_headers[:30])
            )
        frame_index = find_column(headers, "frame")
        gpu_values = trim(numeric_column(rows, gpu_index), args.trim_start, args.trim_end)
        frame_values = (
            trim(numeric_column(rows, frame_index), args.trim_start, args.trim_end)
            if frame_index is not None
            else []
        )
        mode = infer_mode(path, metadata)
        trial = infer_trial(path, metadata)
        per_mode_gpu[mode].extend(gpu_values)
        capture: dict[str, object] = {
            "file": str(path),
            "mode": mode,
            "trial": trial,
            "gpu_column": headers[gpu_index],
            "gpu": describe(gpu_values),
        }
        if trial is not None and trial > 0 and mode in ("fast", "reference"):
            if trial in capture_medians[mode]:
                raise ValueError(f"Duplicate {mode} capture for trial {trial}.")
            capture_medians[mode][trial] = float(capture["gpu"]["median_ms"])  # type: ignore[index]
        if frame_values:
            capture["frame_column"] = headers[frame_index]  # type: ignore[index]
            capture["frame"] = describe(frame_values)
        captures.append(capture)

    if args.require_pair and not {"fast", "reference"}.issubset(per_mode_gpu):
        raise ValueError("Paired results require at least one Fast and one Reference capture.")

    modes: dict[str, dict[str, float | int]] = {}
    for mode, values in sorted(per_mode_gpu.items()):
        stats = describe(values)
        medians = list(capture_medians.get(mode, {}).values())
        stats["capture_median_ms"] = statistics.median(medians) if medians else stats["median_ms"]
        stats["capture_median_cv_percent"] = (
            statistics.stdev(medians) / statistics.fmean(medians) * 100.0
            if len(medians) >= 2 and statistics.fmean(medians) != 0.0
            else 0.0
        )
        modes[mode] = stats
    paired_comparison = build_paired_comparison(capture_medians, args.bootstrap_resamples)
    comparison: dict[str, float] | None = None
    if "fast" in modes and "reference" in modes:
        fast = float(modes["fast"]["median_ms"])
        reference = float(modes["reference"]["median_ms"])
        comparison = {
            "fast_minus_reference_ms": fast - reference,
            "fast_change_percent": (fast - reference) / reference * 100.0,
            "fast_speedup_percent": (reference - fast) / reference * 100.0,
        }

    summary: dict[str, object] = {
        "trim_start_frames": args.trim_start,
        "trim_end_frames": args.trim_end,
        "captures": captures,
        "modes": modes,
        "comparison": comparison,
        "paired_comparison": paired_comparison,
    }

    output_directory = (args.output_directory or csv_paths[0].parent).resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    with (output_directory / "summary.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            (
                "mode",
                "samples",
                "mean_ms",
                "median_ms",
                "p95_ms",
                "min_ms",
                "max_ms",
                "capture_median_ms",
                "capture_median_cv_percent",
            )
        )
        for mode, stats in modes.items():
            writer.writerow(
                (
                    mode,
                    stats["samples"],
                    f'{stats["mean_ms"]:.6f}',
                    f'{stats["median_ms"]:.6f}',
                    f'{stats["p95_ms"]:.6f}',
                    f'{stats["min_ms"]:.6f}',
                    f'{stats["max_ms"]:.6f}',
                    f'{stats["capture_median_ms"]:.6f}',
                    f'{stats["capture_median_cv_percent"]:.6f}',
                )
            )
    if paired_comparison is not None:
        with (output_directory / "paired_trials.csv").open(
            "w", encoding="utf-8", newline=""
        ) as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=(
                    "trial",
                    "fast_median_ms",
                    "reference_median_ms",
                    "fast_minus_reference_ms",
                    "fast_speedup_percent",
                ),
            )
            writer.writeheader()
            writer.writerows(paired_comparison["pairs"])

    for mode, stats in modes.items():
        print(
            f"{mode}: median {stats['median_ms']:.4f} ms, mean {stats['mean_ms']:.4f} ms, "
            f"p95 {stats['p95_ms']:.4f} ms ({stats['samples']} samples)"
        )
    if comparison is not None:
        print(
            "Fast vs Reference: "
            f"{comparison['fast_minus_reference_ms']:+.4f} ms, "
            f"{comparison['fast_change_percent']:+.2f}% (negative is faster)"
        )
    if paired_comparison is not None:
        delta_ci = paired_comparison["mean_delta_95ci_ms"]
        speedup_ci = paired_comparison["mean_speedup_95ci_percent"]
        print(
            f"Paired trials: {paired_comparison['paired_trial_count']}; mean delta "
            f"{paired_comparison['mean_fast_minus_reference_ms']:+.4f} ms "
            f"(95% bootstrap CI {delta_ci[0]:+.4f} to {delta_ci[1]:+.4f})"
        )
        print(
            f"Paired mean speedup: {paired_comparison['mean_fast_speedup_percent']:+.2f}% "
            f"(95% bootstrap CI {speedup_ci[0]:+.2f}% to {speedup_ci[1]:+.2f}%); "
            f"conclusion: {paired_comparison['conclusion']}"
        )
    print(f"Wrote {output_directory / 'summary.json'}")
    print(f"Wrote {output_directory / 'summary.csv'}")
    if paired_comparison is not None:
        print(f"Wrote {output_directory / 'paired_trials.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
