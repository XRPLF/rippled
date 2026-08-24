#!/usr/bin/env python3
"""Tests for check_regression_bounds.py.

The checker reads five files by path relative to the working directory, so each
test assembles a scratch tree holding copies of the real inputs, mutates one
thing, and runs the checker as a subprocess there. Testing the real entry point
is deliberate: the contract under test is the exit code CI reads, and an
in-process call would not exercise it.

Two groups:

* the input-handling contract -- a placeholder baseline must PASS because that
  is the documented bootstrap state, while a missing, unreadable or malformed
  input must FAIL. A checker that returns success without having checked
  anything is the failure this whole gate exists to prevent;
* one case per rule (A to E), so a rule that stops flagging is caught.

stdlib unittest only; the repo installs no third-party runner for CI.
"""

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
CHECKER = SCRIPT_DIR / "check_regression_bounds.py"
REPO = SCRIPT_DIR.parents[2]

WORKLOAD = "docker/telemetry/workload"
BASELINE = f"{WORKLOAD}/baselines/baseline-timings.json"
THRESHOLDS = f"{WORKLOAD}/regression-thresholds.json"
METRICS = f"{WORKLOAD}/regression-metrics.json"
COLLECTOR = "docker/telemetry/otel-collector-config.yaml"
HEADER = "include/xrpl/telemetry/HistogramBuckets.h"
INPUTS = (BASELINE, THRESHOLDS, METRICS, COLLECTOR, HEADER)


class CheckerCase(unittest.TestCase):
    """Base class giving each test an isolated copy of the checker's inputs."""

    def setUp(self):
        self.tree = Path(tempfile.mkdtemp())
        self.addCleanup(self._cleanup)
        for rel in INPUTS:
            dest = self.tree / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(REPO / rel, dest)
        script = self.tree / ".github/scripts/telemetry/check_regression_bounds.py"
        script.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(CHECKER, script)

    def _cleanup(self):
        for path in self.tree.rglob("*"):
            if path.is_file():
                path.chmod(stat.S_IRUSR | stat.S_IWUSR)
        shutil.rmtree(self.tree, ignore_errors=True)

    def run_checker(self):
        """Run the checker in the scratch tree, returning (code, stdout+stderr)."""
        proc = subprocess.run(
            [sys.executable, ".github/scripts/telemetry/check_regression_bounds.py"],
            cwd=self.tree,
            capture_output=True,
            text=True,
        )
        return proc.returncode, proc.stdout + proc.stderr

    def edit_json(self, rel, mutate):
        """Load a scratch input, hand it to mutate(), write it back."""
        path = self.tree / rel
        data = json.loads(path.read_text())
        mutate(data)
        path.write_text(json.dumps(data, indent=2))


class TestInputHandling(CheckerCase):
    """A placeholder passes; a missing or broken input must not."""

    def test_unmodified_tree_passes(self):
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)
        self.assertIn("gated key(s)", out)

    def test_placeholder_flag_passes(self):
        self.edit_json(BASELINE, lambda d: d.update(placeholder=True))
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)
        self.assertIn("placeholder", out)

    def test_empty_metrics_baseline_passes(self):
        self.edit_json(BASELINE, lambda d: d.update(metrics={}))
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)
        self.assertIn("placeholder", out)

    def test_missing_baseline_fails_naming_the_input(self):
        (self.tree / BASELINE).unlink()
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("baseline-timings.json", out)

    def test_missing_collector_config_fails_naming_the_input(self):
        (self.tree / COLLECTOR).unlink()
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("otel-collector-config.yaml", out)

    @unittest.skipIf(os.geteuid() == 0, "root ignores the read permission bit")
    def test_unreadable_baseline_fails(self):
        (self.tree / BASELINE).chmod(0)
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("baseline-timings.json", out)
        self.assertIn("could not be read", out)
        self.assertNotIn("Traceback", out)

    def test_malformed_baseline_json_fails(self):
        (self.tree / BASELINE).write_text("{ not json")
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("valid JSON", out)

    def test_malformed_thresholds_json_fails(self):
        (self.tree / THRESHOLDS).write_text("]")
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("valid JSON", out)


class TestRules(CheckerCase):
    """One case per rule, so a rule that stops flagging is caught."""

    def test_rule_a_flags_baseline_key_not_declared(self):
        self.edit_json(
            BASELINE,
            lambda d: d["metrics"].update(
                {"span.rpc.process.p99": {"unit": "ms", "value": 9.0}}
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule A)", out)

    def test_rule_a_flags_declared_key_without_baseline(self):
        self.edit_json(METRICS, lambda d: d["spans"]["names"].append("consensus.round"))
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule A)", out)

    def test_rule_b_flags_missing_override(self):
        self.edit_json(THRESHOLDS, lambda d: d["overrides"].pop("span.ledger.build"))
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule B)", out)

    def test_rule_c_flags_rounded_bound(self):
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.process"]["p99"].update(
                max_abs_increase_ms=4.0055
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule C)", out)

    def test_rule_c_accepts_bound_within_relative_tolerance(self):
        """The tolerance is 1e-12 relative, not exact equality."""
        exact = 4.005485184848892
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.process"]["p99"].update(
                max_abs_increase_ms=exact * (1 + 5e-13)
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)

    def test_rule_d_flags_percentage_bound_becoming_operative(self):
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.apply"]["p99"].update(
                max_pct_increase=150.0
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule D)", out)

    def test_rule_e_flags_ladder_floor_signature(self):
        """ledger.store's quantiles were the ladder floor times the quantile."""
        store = {"p50": 0.005, "p95": 0.0095, "p99": 0.0099}
        self.edit_json(METRICS, lambda d: d["spans"]["names"].append("ledger.store"))
        self.edit_json(
            BASELINE,
            lambda d: d["metrics"].update(
                {
                    f"span.ledger.store.{q}": {"unit": "ms", "value": v}
                    for q, v in store.items()
                }
            ),
        )
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"].update(
                {
                    "span.ledger.store": {
                        q: {"max_pct_increase": 50.0, "max_abs_increase_ms": 0.05 - v}
                        for q, v in store.items()
                    }
                }
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule E)", out)


if __name__ == "__main__":
    unittest.main()
