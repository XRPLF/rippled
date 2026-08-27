#!/usr/bin/env python3
"""Tests for capture_timings.py's completeness guard.

Run with plain python3 -- there is no pytest in the harness requirements, and
this file needs nothing but the standard library plus the aiohttp that
capture_timings.py already imports:

    python3 docker/telemetry/workload/test_capture_timings.py

What is under test is the rule that decides whether a captured timings file may
become a regression baseline. It is worth pinning because every way of getting
it wrong is silently green: a capture that asked Prometheus for nothing, or got
almost nothing back, still produces a well-formed JSON file. If such a file is
accepted, it is pasted in as a baseline, still reads as a placeholder, and the
regression gate stays off while the workflow reports it as activated.

Both halves are covered: the predicate itself, and the exit code, because the
workflow keys on the exit code while the paste-me step keys on the flag in the
file. They must not be able to disagree.
"""

import asyncio
import json
import sys
import tempfile
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))

import capture_timings as ct  # noqa: E402


def _surface(*values: float | None) -> dict[str, dict[str, Any]]:
    """A metrics dict of len(values) keys, None meaning nothing came back."""
    return {f"metric_{i}": {"value": v} for i, v in enumerate(values)}


def test_empty_surface_is_not_complete() -> None:
    """A capture that declared nothing must never count as complete.

    This is the vacuous-truth trap: 0 of 0 keys is 100% by arithmetic, so a
    plain ratio test calls an empty capture a perfect one. It happens for real
    whenever --metrics points at the wrong or a truncated file, since
    build_query_plan returns an empty plan for that without complaining.

    The production change that makes this fail: dropping the `declared > 0`
    term from the `complete` predicate.
    """
    status = ct._capture_status({}, min_ratio=0.5)
    assert status["declared"] == 0
    assert status["captured"] == 0
    assert status["complete"] is False, status


def test_ratio_exactly_at_the_minimum_is_complete() -> None:
    """The bar is inclusive, so a capture sitting exactly on it passes.

    The production change that makes this fail: using `>` instead of `>=`,
    which would reject a run that met the stated minimum exactly.
    """
    status = ct._capture_status(_surface(1.0, None), min_ratio=0.5)
    assert (status["captured"], status["declared"]) == (1, 2)
    assert status["complete"] is True, status


def test_ratio_just_below_the_minimum_is_not_complete() -> None:
    """One captured key out of three is below half and must be rejected.

    The production change that makes this fail: comparing against a constant,
    or ignoring min_ratio and accepting any non-zero capture.
    """
    status = ct._capture_status(_surface(1.0, None, None), min_ratio=0.5)
    assert (status["captured"], status["declared"]) == (1, 3)
    assert status["complete"] is False, status


def test_null_values_are_declared_but_not_captured() -> None:
    """A key Prometheus had no answer for counts against the ratio.

    Every declared key is present in the artifact, some with value null, so
    counting keys rather than values would report a full capture on a run that
    got nothing back.

    The production change that makes this fail: setting captured to
    len(metrics).
    """
    status = ct._capture_status(_surface(1.0, 2.0, None, None), min_ratio=0.5)
    assert status["declared"] == 4
    assert status["captured"] == 2


def test_the_bar_it_was_judged_against_is_recorded() -> None:
    """The artifact must state its own threshold, not just the verdict.

    Without it a rejected capture cannot be judged after the fact -- 8 of 20 is
    a pass at 0.4 and a failure at 0.5, and the file is the only record of
    which was asked for.
    """
    assert ct._capture_status(_surface(1.0), min_ratio=0.75)["min_ratio"] == 0.75


def _run_main(monkey_report: dict[str, Any]) -> tuple[int, dict[str, Any]]:
    """Run main() against a crafted report, returning (exit code, written file).

    capture() is replaced rather than mocked at the HTTP layer because what is
    under test is what main() does with a report, not how the report is
    obtained.
    """
    original_capture, original_argv = ct.capture, sys.argv

    async def fake_capture(**_kwargs: Any) -> dict[str, Any]:
        return monkey_report

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "timings.json"
        ct.capture = fake_capture
        sys.argv = ["capture_timings.py", "--output", str(out)]
        try:
            code = ct.main()
            written = json.loads(out.read_text())
        finally:
            ct.capture, sys.argv = original_capture, original_argv
    return code, written


def _report(metrics: dict[str, Any], min_ratio: float = 0.5) -> dict[str, Any]:
    """A report shaped like capture()'s, with a real status block."""
    return {
        "schema_version": ct.SCHEMA_VERSION,
        "captured_at": "2026-01-01T00:00:00Z",
        "window": "3m",
        "git_sha": "0" * 40,
        "profile": "regression",
        "capture": ct._capture_status(metrics, min_ratio),
        "metrics": metrics,
    }


def test_incomplete_capture_exits_nonzero_and_says_so_in_the_file() -> None:
    """The exit code and the flag in the file must agree.

    The workflow gates on the exit code while the paste-me step reads the flag,
    so a run where they disagreed would be refused by one and offered as
    baseline material by the other.

    The production change that makes this fail: returning 0 regardless, or
    computing the exit code from something other than status["complete"].
    """
    code, written = _run_main(_report(_surface(1.0, None, None)))
    assert code == 1, f"incomplete capture exited {code}"
    assert written["capture"]["complete"] is False


def test_complete_capture_exits_zero_and_says_so_in_the_file() -> None:
    """The passing direction, so the guard cannot be satisfied by always failing."""
    code, written = _run_main(_report(_surface(1.0, 2.0)))
    assert code == 0, f"complete capture exited {code}"
    assert written["capture"]["complete"] is True


def test_empty_surface_exits_nonzero_without_dividing_by_zero() -> None:
    """The empty case needs its own error path, not the percentage one.

    Reporting "0/0 (0%)" requires captured / total, which raises
    ZeroDivisionError on an empty surface -- turning a clear rejection into a
    traceback, and on some callers into a non-1 exit the workflow reads
    differently.

    The production change that makes this fail: removing the `total == 0`
    branch and letting the percentage message handle every shortfall.
    """
    code, written = _run_main(_report({}))
    assert code == 1, f"empty capture exited {code}"
    assert written["capture"]["declared"] == 0
    assert written["capture"]["complete"] is False


def test_capture_records_a_status_block_carrying_the_requested_ratio() -> None:
    """capture() itself must build the status block, from the ratio it was given.

    The other tests here hand main() a report built by this file, so none of them
    runs the real capture(). Without this, capture() could ignore
    --min-capture-ratio, or stop emitting the capture block at all, and the suite
    would stay green while every downstream consumer lost the flag it keys on.

    Only the Prometheus call is replaced; the query plan is built from the real
    regression-metrics.json.

    The production change that makes this fail: dropping the capture block from
    the returned report, or hardcoding the ratio instead of using the argument.
    """
    original = ct.run_query_plan

    async def fake_run_query_plan(_session: Any, _url: str, _plan: Any) -> dict:
        return {"kept": {"value": 1.0}, "missing": {"value": None}}

    ct.run_query_plan = fake_run_query_plan
    try:
        report = asyncio.run(
            ct.capture(
                prom_url="http://prometheus.invalid",
                metrics_path=Path(__file__).parent / "regression-metrics.json",
                window="3m",
                profile="regression",
                min_capture_ratio=0.75,
            )
        )
    finally:
        ct.run_query_plan = original

    assert "capture" in report, sorted(report)
    status = report["capture"]
    assert status["min_ratio"] == 0.75, status
    assert (status["declared"], status["captured"]) == (2, 1), status
    # 1 of 2 is 0.5, below the 0.75 that was asked for.
    assert status["complete"] is False, status


def test_the_exit_code_follows_the_flag_not_a_recomputed_ratio() -> None:
    """main() must read capture.complete, not judge the counts itself.

    The workflow gates on the exit code and the paste-me step reads the flag, so
    the flag has to be the single source of truth. Recomputing the ratio in
    main() would work today and drift the moment the two rules differed. Both
    directions are asserted, because a mutation that recomputes agrees with the
    flag on every self-consistent report -- only a contradictory one separates
    them.

    The production change that makes this fail: deriving the exit code from
    captured/declared rather than from status["complete"].
    """
    complete_flag_says_no = _report(_surface(1.0, 2.0))
    complete_flag_says_no["capture"]["complete"] = False
    code, _ = _run_main(complete_flag_says_no)
    assert code == 1, "a report flagged incomplete must exit non-zero"

    complete_flag_says_yes = _report(_surface(1.0, None, None))
    complete_flag_says_yes["capture"]["complete"] = True
    code, _ = _run_main(complete_flag_says_yes)
    assert code == 0, "a report flagged complete must exit zero"


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    # Collecting nothing is a failure, not a pass. A rename of the test_ prefix,
    # or running this file from a context where the globals are not populated,
    # would otherwise print "0/0 passed" and exit 0 -- the silent green these
    # tests exist to prevent, reproduced in the runner itself.
    if not tests:
        print("FAIL: no tests were collected")
        return 1
    failed = 0
    for test in tests:
        try:
            test()
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {test.__name__}: {exc}")
        except SystemExit as exc:
            # argparse and other sys.exit() paths raise SystemExit, which is NOT
            # an Exception subclass. Uncaught it aborts the whole file, so the
            # remaining tests never run and nothing prints a FAIL line.
            failed += 1
            print(f"ERROR {test.__name__}: SystemExit({exc.code})")
        except Exception as exc:  # noqa: BLE001 - report any error as a failure
            failed += 1
            print(f"ERROR {test.__name__}: {type(exc).__name__}: {exc}")
        else:
            print(f"PASS {test.__name__}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
