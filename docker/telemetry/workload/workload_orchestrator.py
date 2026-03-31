#!/usr/bin/env python3
"""Workload Orchestrator for rippled telemetry validation.

Reads a named profile from workload-profiles.json and executes sequential
load phases, each with configurable RPC and TX parameters.  Produces a
combined report with per-phase results.

Phases run sequentially.  Within each phase, the RPC load generator and
transaction submitter run concurrently (if both are configured).

Orchestration Flow::

    workload-profiles.json
           |
           v
    workload_orchestrator.py
           |
      +----+----+----+----+----+----+
      | Phase 1 | Phase 2 | ...... | Phase N |
      +----+----+----+----+----+----+
           |              |
      +----+----+    +----+----+
      | rpc_load |    | tx_sub  |   (concurrent within phase)
      | _gen.py  |    | mitter  |
      +----+----+    +----+----+
           |              |
           v              v
      per-phase JSON reports
           |
           v
      combined-report.json

Usage:
    python3 workload_orchestrator.py --profile full-validation
    python3 workload_orchestrator.py --profile quick-smoke --endpoints ws://localhost:6006
    python3 workload_orchestrator.py --profile stress --report /tmp/report.json

Profiles are defined in workload-profiles.json in the same directory.
"""

import argparse
import asyncio
import json
import logging
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

logger = logging.getLogger("workload_orchestrator")

SCRIPT_DIR = Path(__file__).parent.resolve()
PROFILES_FILE = SCRIPT_DIR / "workload-profiles.json"


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class PhaseResult:
    """Result of a single workload phase.

    Attributes:
        name:         Phase name from the profile.
        duration_sec: Configured duration.
        actual_sec:   Actual elapsed time.
        rpc_summary:  JSON summary from rpc_load_generator, or None.
        tx_summary:   JSON summary from tx_submitter, or None.
        errors:       List of error messages from subprocess failures.
    """

    name: str
    duration_sec: int
    actual_sec: float = 0.0
    rpc_summary: dict[str, Any] | None = None
    tx_summary: dict[str, Any] | None = None
    errors: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Profile loading
# ---------------------------------------------------------------------------


def load_profile(profile_name: str) -> dict[str, Any]:
    """Load a named profile from workload-profiles.json.

    Args:
        profile_name: Key in the profiles dict (e.g., "full-validation").

    Returns:
        The profile dict with phases and propagation_wait_sec.

    Raises:
        SystemExit: If the profile file or name is not found.
    """
    if not PROFILES_FILE.exists():
        logger.error("Profiles file not found: %s", PROFILES_FILE)
        sys.exit(2)

    with open(PROFILES_FILE) as f:
        data = json.load(f)

    profiles = data.get("profiles", {})
    if profile_name not in profiles:
        available = ", ".join(profiles.keys())
        logger.error("Profile '%s' not found. Available: %s", profile_name, available)
        sys.exit(2)

    profile = profiles[profile_name]

    # Validate profile schema — fail fast on bad config.
    phases = profile.get("phases", [])
    if not isinstance(phases, list) or not phases:
        logger.error("Profile '%s' has no valid phases", profile_name)
        sys.exit(2)
    for i, phase in enumerate(phases):
        if not isinstance(phase.get("name"), str):
            logger.error("Phase %d missing valid 'name'", i)
            sys.exit(2)
        if (
            not isinstance(phase.get("duration_sec"), (int, float))
            or phase["duration_sec"] <= 0
        ):
            logger.error(
                "Phase %d '%s' has invalid duration_sec",
                i,
                phase.get("name"),
            )
            sys.exit(2)

    logger.info(
        "Loaded profile '%s': %s (%d phases)",
        profile_name,
        profile.get("description", ""),
        len(phases),
    )
    return profile


# ---------------------------------------------------------------------------
# Subprocess execution
# ---------------------------------------------------------------------------


async def run_subprocess(cmd: list[str], label: str) -> tuple[int, str, str]:
    """Run a subprocess and capture its stdout and stderr.

    Args:
        cmd:   Command and arguments.
        label: Human-readable label for logging.

    Returns:
        Tuple of (return_code, stdout_text, stderr_text).
    """
    logger.debug("Starting %s: %s", label, " ".join(cmd))
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await proc.communicate()
    if proc.returncode != 0:
        logger.warning(
            "%s exited with code %d: %s",
            label,
            proc.returncode,
            stderr.decode().strip()[-500:],
        )
    return proc.returncode, stdout.decode(), stderr.decode()


# ---------------------------------------------------------------------------
# Phase execution
# ---------------------------------------------------------------------------


def _collect_task_result(
    label: str,
    returncode: int,
    stderr: str,
    report_path: Path,
    result: PhaseResult,
) -> None:
    """Process the result of a completed subprocess task.

    Reads the JSON report file (if it exists) and records any errors.

    Args:
        label:       "rpc" or "tx".
        returncode:  Subprocess exit code.
        stderr:      Captured stderr text.
        report_path: Path to the JSON report file.
        result:      PhaseResult to update.
    """
    if report_path.exists():
        try:
            with open(report_path) as f:
                summary = json.load(f)
            if label == "rpc":
                result.rpc_summary = summary
            elif label == "tx":
                result.tx_summary = summary
        except (json.JSONDecodeError, OSError) as exc:
            logger.warning("Failed to parse %s report %s: %s", label, report_path, exc)
            result.errors.append(f"Failed to parse {label} report: {exc}")

    if returncode != 0:
        snippet = stderr.strip()[-200:] if stderr else ""
        result.errors.append(
            f"{label.upper()} generator exited with {returncode}: {snippet}"
        )


def _build_rpc_cmd(
    endpoints: list[str], rpc_cfg: dict[str, Any], duration: int, output: Path
) -> list[str]:
    """Build the command list for the RPC load generator subprocess."""
    cmd = [
        sys.executable,
        str(SCRIPT_DIR / "rpc_load_generator.py"),
        "--endpoints",
        *endpoints,
        "--rate",
        str(rpc_cfg.get("rate", 50)),
        "--duration",
        str(duration),
        "--output",
        str(output),
    ]
    weights = rpc_cfg.get("weights")
    if weights:
        cmd.extend(["--weights", json.dumps(weights)])
    return cmd


def _build_tx_cmd(
    endpoint: str, tx_cfg: dict[str, Any], duration: int, output: Path
) -> list[str]:
    """Build the command list for the TX submitter subprocess."""
    cmd = [
        sys.executable,
        str(SCRIPT_DIR / "tx_submitter.py"),
        "--endpoint",
        endpoint,
        "--tps",
        str(tx_cfg.get("tps", 5)),
        "--duration",
        str(duration),
        "--output",
        str(output),
    ]
    weights = tx_cfg.get("weights")
    if weights:
        cmd.extend(["--weights", json.dumps(weights)])
    return cmd


async def run_phase(
    phase: dict[str, Any],
    endpoints: list[str],
    report_dir: Path,
    phase_idx: int,
) -> PhaseResult:
    """Execute a single workload phase.

    Launches rpc_load_generator.py and/or tx_submitter.py as subprocesses
    based on the phase configuration.  Both run concurrently if configured.

    Args:
        phase:      Phase dict from the profile.
        endpoints:  List of WebSocket endpoint URLs.
        report_dir: Directory for per-phase JSON reports.
        phase_idx:  Phase index (for file naming).

    Returns:
        PhaseResult with subprocess outputs.
    """
    name = phase["name"]
    duration = phase["duration_sec"]
    result = PhaseResult(name=name, duration_sec=duration)
    prefix = f"phase{phase_idx + 1}-{name}"

    logger.info(
        "=== Phase %d: %s (%ds) — %s ===",
        phase_idx + 1,
        name,
        duration,
        phase.get("description", ""),
    )

    tasks: list[tuple[str, Path, asyncio.Task]] = []
    t0 = time.monotonic()

    rpc_cfg = phase.get("rpc")
    if rpc_cfg:
        rpc_out = report_dir / f"{prefix}-rpc.json"
        cmd = _build_rpc_cmd(endpoints, rpc_cfg, duration, rpc_out)
        tasks.append(
            ("rpc", rpc_out, asyncio.create_task(run_subprocess(cmd, f"RPC [{name}]")))
        )

    tx_cfg = phase.get("tx")
    if tx_cfg:
        tx_out = report_dir / f"{prefix}-tx.json"
        cmd = _build_tx_cmd(endpoints[0], tx_cfg, duration, tx_out)
        tasks.append(
            ("tx", tx_out, asyncio.create_task(run_subprocess(cmd, f"TX [{name}]")))
        )

    if not tasks:
        logger.warning(
            "Phase %d: %s — no workload configured, skipping", phase_idx + 1, name
        )
        return result

    for label, report_path, task in tasks:
        returncode, _stdout, stderr = await task
        _collect_task_result(label, returncode, stderr, report_path, result)

    result.actual_sec = time.monotonic() - t0
    logger.info(
        "Phase %d complete: %.1fs actual, %d errors",
        phase_idx + 1,
        result.actual_sec,
        len(result.errors),
    )
    return result


# ---------------------------------------------------------------------------
# Profile execution
# ---------------------------------------------------------------------------


async def run_profile(
    profile: dict[str, Any],
    endpoints: list[str],
    report_dir: Path,
) -> dict[str, Any]:
    """Execute all phases in a profile sequentially.

    Args:
        profile:    Profile dict with phases and propagation_wait_sec.
        endpoints:  WebSocket endpoints for the rippled cluster.
        report_dir: Directory for phase reports.

    Returns:
        Combined report dict with per-phase results and totals.
    """
    phases = profile.get("phases", [])
    propagation_wait = profile.get("propagation_wait_sec", 60)
    results: list[PhaseResult] = []

    total_start = time.monotonic()

    for idx, phase in enumerate(phases):
        result = await run_phase(phase, endpoints, report_dir, idx)
        results.append(result)

    # Wait for telemetry data to propagate through the collector pipeline.
    logger.info("Waiting %ds for telemetry data to propagate...", propagation_wait)
    await asyncio.sleep(propagation_wait)

    total_elapsed = time.monotonic() - total_start

    # Build combined report from all phase results.
    total_rpc_sent = 0
    total_rpc_errors = 0
    total_tx_submitted = 0
    total_tx_errors = 0
    phase_reports = []

    for r in results:
        pr: dict[str, Any] = {
            "name": r.name,
            "duration_sec": r.duration_sec,
            "actual_sec": round(r.actual_sec, 1),
            "errors": r.errors,
        }
        if r.rpc_summary:
            pr["rpc"] = r.rpc_summary
            total_rpc_sent += r.rpc_summary.get("total_sent", 0)
            total_rpc_errors += r.rpc_summary.get("total_errors", 0)
        if r.tx_summary:
            pr["tx"] = r.tx_summary
            total_tx_submitted += r.tx_summary.get("total_submitted", 0)
            total_tx_errors += r.tx_summary.get("total_errors", 0)
        phase_reports.append(pr)

    report = {
        "profile": profile.get("description", ""),
        "total_elapsed_sec": round(total_elapsed, 1),
        "phases": phase_reports,
        "totals": {
            "rpc_sent": total_rpc_sent,
            "rpc_errors": total_rpc_errors,
            "tx_submitted": total_tx_submitted,
            "tx_errors": total_tx_errors,
        },
    }

    return report


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Workload Orchestrator for rippled telemetry validation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Profiles:
  full-validation  Full 18-dashboard coverage (~5 min load + 1 min propagation)
  quick-smoke      Fast CI smoke test (~30s load + 30s propagation)
  stress           Heavy sustained load for benchmarking (~3.5 min + 1 min)

Examples:
  python3 workload_orchestrator.py --profile full-validation
  python3 workload_orchestrator.py --profile quick-smoke --endpoints ws://localhost:6006
  python3 workload_orchestrator.py --profile stress --report /tmp/report.json
        """,
    )
    parser.add_argument(
        "--profile",
        type=str,
        required=True,
        help="Named profile from workload-profiles.json",
    )
    parser.add_argument(
        "--endpoints",
        nargs="+",
        default=["ws://localhost:6006"],
        help="WebSocket endpoints (default: ws://localhost:6006)",
    )
    parser.add_argument(
        "--report",
        type=str,
        default=None,
        help="Write combined JSON report to this file",
    )
    parser.add_argument(
        "--report-dir",
        type=str,
        default="/tmp/xrpld-validation/reports",
        help="Directory for per-phase reports",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    return parser.parse_args()


def main() -> None:
    """Main entry point for the workload orchestrator."""
    args = parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    )

    profile = load_profile(args.profile)
    report_dir = Path(args.report_dir)
    report_dir.mkdir(parents=True, exist_ok=True)

    report = asyncio.run(run_profile(profile, args.endpoints, report_dir))

    print(json.dumps(report, indent=2))

    if args.report:
        with open(args.report, "w") as f:
            json.dump(report, f, indent=2)
        logger.info("Combined report written to %s", args.report)

    # Exit with error if either generator had high error rates.
    totals = report["totals"]
    rpc_err_rate = (
        totals["rpc_errors"] / totals["rpc_sent"] * 100 if totals["rpc_sent"] > 0 else 0
    )
    tx_err_rate = (
        totals["tx_errors"] / totals["tx_submitted"] * 100
        if totals["tx_submitted"] > 0
        else 0
    )
    if rpc_err_rate > 50 or tx_err_rate > 50:
        logger.error(
            "High error rates: RPC=%.1f%%, TX=%.1f%%", rpc_err_rate, tx_err_rate
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
