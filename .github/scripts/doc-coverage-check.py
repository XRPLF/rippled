#!/usr/bin/env python3
"""
Documentation coverage checker for xrpld.

Parses coverxygen LCOV output, compares against per-module thresholds
defined in .github/doc-coverage-thresholds.json, and generates a
markdown report suitable for posting as a PR comment.

Usage:
    python3 doc-coverage-check.py \
        --lcov-file doc-coverage.info \
        --threshold-file .github/doc-coverage-thresholds.json \
        --output doc-coverage-report.md \
        [--base-lcov-file base-doc-coverage.info]
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


def parse_lcov(lcov_path: str) -> dict[str, dict[str, int]]:
    """Parse LCOV-format file into per-file coverage data.

    Returns a dict mapping file paths to {"documented": N, "total": N}.
    """
    coverage = {}
    current_file = None
    documented = 0
    total = 0

    with open(lcov_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("SF:"):
                current_file = line[3:]
                documented = 0
                total = 0
            elif line.startswith("DA:"):
                parts = line[3:].split(",")
                if len(parts) >= 2:
                    total += 1
                    if int(parts[1]) > 0:
                        documented += 1
            elif line == "end_of_record":
                if current_file:
                    coverage[current_file] = {
                        "documented": documented,
                        "total": total,
                    }
                current_file = None

    return coverage


def compute_module_coverage(
    coverage: dict[str, dict[str, int]],
    module_prefixes: list[str],
) -> dict[str, dict[str, int | float]]:
    """Aggregate file-level coverage into module-level stats."""
    modules = {}
    for prefix in module_prefixes:
        doc = 0
        tot = 0
        for filepath, stats in coverage.items():
            if filepath.startswith(prefix) or f"/{prefix}" in filepath:
                doc += stats["documented"]
                tot += stats["total"]
        pct = (doc / tot * 100) if tot > 0 else 0.0
        modules[prefix] = {"documented": doc, "total": tot, "percent": round(pct, 1)}
    return modules


def compute_global_coverage(
    coverage: dict[str, dict[str, int]],
) -> dict[str, int | float]:
    """Compute overall coverage across all files."""
    doc = sum(s["documented"] for s in coverage.values())
    tot = sum(s["total"] for s in coverage.values())
    pct = (doc / tot * 100) if tot > 0 else 0.0
    return {"documented": doc, "total": tot, "percent": round(pct, 1)}


def check_ratchet(
    current: dict[str, dict[str, int | float]],
    base: dict[str, dict[str, int | float]] | None,
    current_global: dict[str, int | float],
    base_global: dict[str, int | float] | None,
) -> list[str]:
    """Check that no module or global coverage decreased vs base branch."""
    violations = []

    if base_global and current_global["percent"] < base_global["percent"]:
        violations.append(
            f"Global coverage decreased: {base_global['percent']}% -> "
            f"{current_global['percent']}%"
        )

    if base:
        for module, stats in current.items():
            if module in base and stats["percent"] < base[module]["percent"]:
                violations.append(
                    f"`{module}` coverage decreased: "
                    f"{base[module]['percent']}% -> {stats['percent']}%"
                )

    return violations


def check_new_files(
    coverage: dict[str, dict[str, int]],
    new_files: list[str],
    min_coverage: int,
) -> list[str]:
    """Check that new files meet minimum documentation coverage."""
    violations = []
    for filepath in new_files:
        for covered_path, stats in coverage.items():
            if filepath in covered_path or covered_path.endswith(filepath):
                if stats["total"] > 0:
                    pct = stats["documented"] / stats["total"] * 100
                    if pct < min_coverage:
                        violations.append(
                            f"`{filepath}` has {pct:.0f}% doc coverage "
                            f"(minimum {min_coverage}%)"
                        )
                break
    return violations


def coverage_emoji(pct: float) -> str:
    if pct >= 80:
        return "+"
    if pct >= 50:
        return "~"
    return "-"


def generate_report(
    global_stats: dict[str, int | float],
    module_stats: dict[str, dict[str, int | float]],
    thresholds: dict,
    violations: list[str],
    new_file_violations: list[str],
) -> str:
    """Generate a markdown report for the PR comment."""
    lines = []
    lines.append("## Documentation Coverage Report")
    lines.append("")

    passed = not violations and not new_file_violations
    status = "PASSED" if passed else "FAILED"
    lines.append(f"**Status:** {status}")
    lines.append(
        f"**Global Coverage:** {global_stats['percent']}% "
        f"({global_stats['documented']}/{global_stats['total']} entities documented)"
    )
    lines.append(
        f"**Minimum Threshold:** {thresholds.get('global_minimum', 0)}%"
    )
    lines.append("")

    if violations or new_file_violations:
        lines.append("### Violations")
        lines.append("")
        for v in violations + new_file_violations:
            lines.append(f"- {v}")
        lines.append("")

    lines.append("### Module Coverage")
    lines.append("")
    lines.append("| Module | Coverage | Documented | Total | Threshold |")
    lines.append("|--------|----------|------------|-------|-----------|")

    module_thresholds = thresholds.get("module_thresholds", {})
    for module in sorted(module_stats.keys()):
        stats = module_stats[module]
        threshold = module_thresholds.get(module, 0)
        emoji = coverage_emoji(stats["percent"])
        lines.append(
            f"| `{module}` | {stats['percent']}% | "
            f"{stats['documented']} | {stats['total']} | {threshold}% |"
        )

    lines.append("")
    lines.append(
        "*Coverage measured by [coverxygen](https://github.com/psycofdj/coverxygen). "
        "See [docs/DOCUMENTATION_STANDARDS.md](../docs/DOCUMENTATION_STANDARDS.md) "
        "for documentation guidelines.*"
    )

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Check documentation coverage")
    parser.add_argument("--lcov-file", required=True, help="Path to LCOV coverage file")
    parser.add_argument(
        "--threshold-file", required=True, help="Path to thresholds JSON"
    )
    parser.add_argument("--output", required=True, help="Path to write markdown report")
    parser.add_argument(
        "--base-lcov-file", default=None, help="Path to base branch LCOV file"
    )
    parser.add_argument(
        "--new-files",
        default="",
        help="Comma-separated list of new C++ files in this PR",
    )
    args = parser.parse_args()

    with open(args.threshold_file) as f:
        thresholds = json.load(f)

    coverage = parse_lcov(args.lcov_file)
    module_prefixes = list(thresholds.get("module_thresholds", {}).keys())
    module_stats = compute_module_coverage(coverage, module_prefixes)
    global_stats = compute_global_coverage(coverage)

    base_coverage = None
    base_module_stats = None
    base_global_stats = None
    if args.base_lcov_file and Path(args.base_lcov_file).exists():
        base_coverage = parse_lcov(args.base_lcov_file)
        base_module_stats = compute_module_coverage(base_coverage, module_prefixes)
        base_global_stats = compute_global_coverage(base_coverage)

    violations = []

    if global_stats["percent"] < thresholds.get("global_minimum", 0):
        violations.append(
            f"Global coverage {global_stats['percent']}% is below minimum "
            f"{thresholds['global_minimum']}%"
        )

    for module, threshold in thresholds.get("module_thresholds", {}).items():
        if module in module_stats and module_stats[module]["percent"] < threshold:
            violations.append(
                f"`{module}` coverage {module_stats[module]['percent']}% is below "
                f"threshold {threshold}%"
            )

    if thresholds.get("ratchet_mode") == "no_decrease":
        violations.extend(
            check_ratchet(
                module_stats, base_module_stats, global_stats, base_global_stats
            )
        )

    new_file_violations = []
    if args.new_files:
        new_files = [f.strip() for f in args.new_files.split(",") if f.strip()]
        new_file_min = thresholds.get("new_file_minimum", 80)
        new_file_violations = check_new_files(coverage, new_files, new_file_min)

    report = generate_report(
        global_stats, module_stats, thresholds, violations, new_file_violations
    )

    with open(args.output, "w") as f:
        f.write(report)

    print(report)

    if violations or new_file_violations:
        print(f"\nFAILED: {len(violations) + len(new_file_violations)} violation(s)")
        sys.exit(1)
    else:
        print("\nPASSED: All coverage thresholds met")
        sys.exit(0)


if __name__ == "__main__":
    main()
