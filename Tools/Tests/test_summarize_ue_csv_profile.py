import json
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys


TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import SummarizeUECsvProfile as summary  # noqa: E402


class UnrealCsvParserTests(unittest.TestCase):
    @staticmethod
    def write_capture(path: Path, mode: str, trial: int, gpu_ms: float) -> None:
        rows = [f",16.7,{gpu_ms + offset:.3f}" for offset in (0.1, 0.0, -0.1, 0.0, 0.1)]
        rows.extend(
            (
                "EVENTS,FrameTime,GPU/Unaccounted",
                "[HasHeaderRowAtEnd],1",
                f"[KullaContyMode],{mode}",
                f"[KullaContyTrial],{trial}",
            )
        )
        path.write_text("\n".join(rows) + "\n", encoding="utf-8")

    def test_reads_header_at_end_and_metadata(self) -> None:
        content = """\
,16.7,5.0
,16.6,4.8
EVENTS,FrameTime,GPU/Unaccounted
[HasHeaderRowAtEnd],1
[KullaContyMode],Fast
[KullaContyTrial],2
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.csv"
            path.write_text(content, encoding="utf-8")
            headers, rows, metadata = summary.read_unreal_csv(path)

        self.assertEqual(headers, ["EVENTS", "FrameTime", "GPU/Unaccounted"])
        self.assertEqual(len(rows), 2)
        self.assertEqual(summary.find_column(headers, "gpu"), 2)
        self.assertEqual(summary.find_column(headers, "frame"), 1)
        self.assertEqual(summary.infer_mode(path, metadata), "fast")
        self.assertEqual(summary.infer_trial(path, metadata), 2)

    def test_paired_comparison_detects_consistent_fast_win(self) -> None:
        medians = {
            "fast": {1: 4.8, 2: 5.0, 3: 4.9},
            "reference": {1: 5.8, 2: 6.0, 3: 5.9},
        }
        result = summary.build_paired_comparison(medians, bootstrap_resamples=2000)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result["paired_trial_count"], 3)
        self.assertEqual(result["conclusion"], "fast_faster")
        self.assertLess(result["mean_delta_95ci_ms"][1], 0.0)
        self.assertGreater(result["mean_speedup_95ci_percent"][0], 0.0)

    def test_fewer_than_three_pairs_is_not_a_performance_claim(self) -> None:
        medians = {"fast": {1: 4.8}, "reference": {1: 5.8}}
        result = summary.build_paired_comparison(medians, bootstrap_resamples=100)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result["conclusion"], "insufficient_trials")

    def test_command_line_writes_paired_reports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for trial in range(1, 4):
                self.write_capture(root / f"Fast_{trial}.csv", "Fast", trial, 5.0)
                self.write_capture(root / f"Reference_{trial}.csv", "Reference", trial, 6.0)

            completed = subprocess.run(
                (
                    sys.executable,
                    str(TOOLS_DIRECTORY / "SummarizeUECsvProfile.py"),
                    str(root),
                    "--trim-start",
                    "1",
                    "--trim-end",
                    "1",
                    "--bootstrap-resamples",
                    "200",
                    "--require-pair",
                ),
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads((root / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(report["paired_comparison"]["paired_trial_count"], 3)
            self.assertEqual(report["paired_comparison"]["conclusion"], "fast_faster")
            self.assertTrue((root / "summary.csv").is_file())
            self.assertTrue((root / "paired_trials.csv").is_file())

            # Generated CSV reports must not be mistaken for raw Unreal captures
            # when the summarizer is rerun in the same directory.
            rerun = subprocess.run(completed.args, check=False, capture_output=True, text=True)
            self.assertEqual(rerun.returncode, 0, rerun.stderr)


if __name__ == "__main__":
    unittest.main()
