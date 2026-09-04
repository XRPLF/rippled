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
* one case per rule (A to F), so a rule that stops flagging is caught.

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
        # Bound to THIS tree, not read off self.tree when the cleanup finally
        # runs. A test that calls setUp again for a fresh tree (see the
        # degenerate-baseline subTests) rebinds self.tree, and a late read would
        # make every registered cleanup remove the LAST tree, leaving each
        # earlier one behind in /tmp.
        self.addCleanup(self._cleanup, self.tree)
        for rel in INPUTS:
            dest = self.tree / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(REPO / rel, dest)
        script = self.tree / ".github/scripts/telemetry/check_regression_bounds.py"
        script.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(CHECKER, script)

    def _cleanup(self, tree):
        """Remove one scratch tree, restoring permissions rmtree needs first."""
        for path in tree.rglob("*"):
            if path.is_file():
                path.chmod(stat.S_IRUSR | stat.S_IWUSR)
        shutil.rmtree(tree, ignore_errors=True)

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

    def read_json(self, rel):
        """Read a scratch input without modifying it."""
        return json.loads((self.tree / rel).read_text())

    def gated(self, key):
        """Return ``(baseline_value, configured_bound)`` for one gated key.

        Read from the scratch copies of the real inputs rather than written as
        literals, because a literal here is a copy of one particular baseline:
        a hard-coded figure breaks the moment the baseline is refreshed, which is
        the very drift check_regression_bounds.py exists to catch. Deriving the
        figure keeps the assertion pinned to the rule instead of to a snapshot.
        """
        group, quantile = key.rsplit(".", 1)
        rule = self.read_json(THRESHOLDS)["overrides"][group][quantile]
        bound = rule.get("max_abs_increase_ms", rule.get("max_abs_increase_us"))
        return self.read_json(BASELINE)["metrics"][key]["value"], bound


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
        """A bound rounded for readability is still not the derived bound."""
        _, exact = self.gated("span.tx.process.p99")
        rounded = round(exact, 4)
        self.assertNotEqual(rounded, exact, "pick a key whose bound rounds visibly")
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.process"]["p99"].update(
                max_abs_increase_ms=rounded
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule C)", out)

    def test_rule_c_accepts_bound_within_relative_tolerance(self):
        """The tolerance is 1e-12 relative, not exact equality."""
        _, exact = self.gated("span.tx.process.p99")
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.process"]["p99"].update(
                max_abs_increase_ms=exact * (1 + 5e-13)
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)

    def test_rule_d_flags_percentage_bound_becoming_operative(self):
        """Rule D trips at exactly 100 x bound / baseline, its ``>=`` boundary."""
        baseline, bound = self.gated("span.tx.apply.p99")
        boundary = 100.0 * bound / baseline
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.apply"]["p99"].update(
                max_pct_increase=boundary
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule D)", out)

    def test_rule_d_accepts_percentage_bound_just_below_the_boundary(self):
        """The other side of rule D's boundary, so the test cannot pass vacuously."""
        baseline, bound = self.gated("span.tx.apply.p99")
        boundary = 100.0 * bound / baseline
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.tx.apply"]["p99"].update(
                max_pct_increase=boundary * (1 - 1e-9)
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)

    def test_rule_a_ignores_an_excluded_key(self):
        """An excluded key must not read as a baseline that was never captured.

        The unmodified tree already exercises this — span.ledger.validate p95
        and p99 are declared by the names x quantiles product, excluded, and
        absent from the baseline — so this asserts the subtraction is what makes
        it pass, by naming the keys in the reported exclusion line.
        """
        code, out = self.run_checker()
        self.assertEqual(code, 0, out)
        self.assertIn("span.ledger.validate.p95", out)
        self.assertIn("deliberately not gated", out)

    def test_rule_f_flags_exclusion_that_subtracts_nothing(self):
        """A misspelt or stale exclusion silently narrows nothing — catch it."""
        self.edit_json(
            METRICS,
            lambda d: d["excluded_keys"].update({"span.ledger.validate.p97": "typo"}),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule F)", out)
        self.assertIn("subtracts nothing", out)

    def test_rule_f_flags_exclusion_without_a_reason(self):
        self.edit_json(
            METRICS,
            lambda d: d["excluded_keys"].update({"span.ledger.validate.p95": "   "}),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule F)", out)
        self.assertIn("no reason", out)

    def test_rule_f_flags_override_left_behind(self):
        """An excluded key still carrying a bound reads as gated."""
        self.edit_json(
            THRESHOLDS,
            lambda d: d["overrides"]["span.ledger.validate"].update(
                {"p95": {"max_pct_increase": 50.0, "max_abs_increase_ms": 0.25}}
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule F)", out)
        self.assertIn("still has a threshold override", out)

    def test_rule_f_flags_baseline_value_left_behind(self):
        """Excluded but still in the baseline: rule A passes, nothing gates."""
        self.edit_json(
            BASELINE,
            lambda d: d["metrics"].update(
                {"span.ledger.validate.p95": {"unit": "ms", "value": 0.24}}
            ),
        )
        code, out = self.run_checker()
        self.assertEqual(code, 1, out)
        self.assertIn("(rule F)", out)
        self.assertIn("still has a baseline value", out)

    # A key that is GATED, so seeding it exercises the numeric guard rather than
    # rule F. It must not be one of the excluded keys: putting a value there
    # trips "excluded but still has a baseline value" first and the test would
    # pass for the wrong reason.
    DEGENERATE_KEY = "span.tx.process.p50"

    def _seed_degenerate_baseline(self, value):
        """Replace one gated key's baseline value with an unusable one.

        The threshold is deliberately left alone. The numeric guard runs before
        every rule, so no bound arrangement is needed to reach it -- and on the
        pre-fix checker this same seeding still reached the crash, because rule C
        appends its failure and falls through to rule D's division.
        """
        key = self.DEGENERATE_KEY
        self.assertNotIn(
            key,
            self.read_json(METRICS).get("excluded_keys", {}),
            "DEGENERATE_KEY must be gated, not excluded",
        )
        self.edit_json(
            BASELINE,
            lambda d: d["metrics"].update({key: {"unit": "ms", "value": value}}),
        )

    def test_degenerate_baseline_is_reported_not_crashed(self):
        """A zero, negative or non-numeric baseline must NAME the key, not raise.

        Rule D computes ``100 * bound / value``, so a 0.0 baseline used to exit
        via ZeroDivisionError and a traceback, and a negative one used to emit a
        rule-D failure blaming ``max_pct_increase`` when the fault was the
        baseline. Rule E does not cover the zero case: its test reduces to
        ``quantile <= 1e-9`` there, false for every quantile captured.
        """
        # Asserting the guard's OWN wording, not merely "exit 1 with no
        # traceback": the negative and bool cases already exited 1 without a
        # traceback before the fix, by emitting a rule-D failure that blamed the
        # wrong file. A looser assertion passes on that and proves nothing.
        cases = (
            (0.0, "strictly positive"),
            (-1.0, "strictly positive"),
            (float("nan"), "strictly positive"),
            (float("inf"), "strictly positive"),
            ("0.006", "is not a number"),
            (True, "is not a number"),
        )
        for value, expected in cases:
            with self.subTest(value=value):
                self.setUp()  # a clean scratch tree per value
                self._seed_degenerate_baseline(value)
                code, out = self.run_checker()
                self.assertEqual(code, 1, out)
                self.assertNotIn("Traceback", out)
                self.assertIn(self.DEGENERATE_KEY, out)
                self.assertIn(expected, out)
                # The old rule-D message told the maintainer to lower the
                # percentage bound; the baseline is what is wrong.
                self.assertNotIn("(rule D)", out)

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
