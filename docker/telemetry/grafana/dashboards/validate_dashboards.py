#!/usr/bin/env python3
"""Dashboard lint: cumulative metrics must be rate()-wrapped; tier filters present."""

import json, re, sys
from pathlib import Path

# Prometheus gauges that hold a CUMULATIVE total -> must be rate()/increase()-wrapped.
CUMULATIVE_PREFIXES = (
    "total_bytes_",
    "total_messages_",
    "transactions_messages_",
    "transactions_duplicate_messages_",
    "proposals_",
    "validations_",
    "overlay_peer_disconnects",
    "squelch_",
    "overhead_",
    "validator_lists_",
    "set_get_",
    "set_share_",
    "have_transactions_",
    "requested_transactions_",
    "proof_path_",
    "replay_delta_",
    "ledger_data_",
    "ledger_get_",
    "ledger_share_",
    "ledger_transaction_",
    "ledger_account_state_",
    "getobject_",
    "jq_trans_overflow_total",
)
# nodestore_state{metric=...} cumulative sub-series (raw only inside these metric= labels).
NODESTORE_CUMULATIVE = (
    "node_reads_total",
    "node_reads_hit",
    "node_writes",
    "node_read_bytes",
    "node_written_bytes",
    "node_reads_duration_us",
)
# state_accounting_*_duration are cumulative µs.
STATE_DURATION = re.compile(r"state_accounting_\w+_duration")
REQUIRED_FILTERS = (
    "$node",
    "$deployment_environment",
    "$xrpl_network_type",
    "$service_name",
)


def iter_panels(dash):
    for p in dash.get("panels", []):
        yield p
        for sub in p.get("panels", []) or []:
            yield sub


def expr_is_wrapped(expr):
    return (
        "rate(" in expr
        or "increase(" in expr
        or "irate(" in expr
        or "histogram_quantile(" in expr
    )


def check(path, forbid_5m):
    errs = []
    try:
        dash = json.load(open(path))
    except Exception as e:
        return [f"{path}: INVALID JSON: {e}"]
    for p in iter_panels(dash):
        title = p.get("title", "<untitled>")
        for tg in p.get("targets", []) or []:
            expr = (tg.get("expr") or "").strip()
            if not expr:
                continue
            # tier filters
            if "{" in expr:
                for f in REQUIRED_FILTERS:
                    if f not in expr:
                        errs.append(f"{path} [{title}]: missing {f} in expr")
            # cumulative metric plotted raw?
            hits = [m for m in CUMULATIVE_PREFIXES if m in expr]
            nod = [m for m in NODESTORE_CUMULATIVE if f'"{m}"' in expr]
            statedur = STATE_DURATION.search(expr)
            if (hits or nod or statedur) and not expr_is_wrapped(expr):
                who = hits or nod or [statedur.group(0)]
                errs.append(
                    f"{path} [{title}]: cumulative metric {who} plotted RAW (needs rate/increase)"
                )
            if "deriv(" in expr or "idelta(" in expr:
                errs.append(
                    f"{path} [{title}]: uses deriv()/idelta() on a cumulative series"
                )
            if forbid_5m and ("[5m]" in expr or "[1h]" in expr):
                # Only a fixed [5m]/[1h] inside a top-level rate()/increase()/irate() on a
                # plain counter is a violation. Legitimate fixed-window exprs -- histogram
                # buckets (rate(..._bucket[5m])), avg_over_time windows, and subqueries
                # ([5m:]) -- are intentional and must not be flagged.
                if not ("_bucket" in expr or "avg_over_time" in expr or ":]" in expr):
                    errs.append(
                        f"{path} [{title}]: hardcoded range window; use [$__rate_interval]"
                    )
        # unit check
        unit = p.get("fieldConfig", {}).get("defaults", {}).get("unit", "")
        if unit == "mps":
            errs.append(f"{path} [{title}]: invalid Grafana unit 'mps' (use 'cps')")
    return errs


def main():
    """Validate the dashboards named on the command line, or all of them.

    Exit status: 0 every dashboard passed, 1 violations were found, 2 there was
    nothing to validate. The last is an error rather than a pass: reporting
    success without having read a single dashboard is indistinguishable from a
    clean run, so a caller that mis-spells a path gets a green light for work
    that never happened.
    """
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    forbid_5m = "--no-5m" in sys.argv
    if not args:
        # Default to every dashboard beside this script, so a bare run checks
        # the whole set instead of iterating an empty list.
        args = sorted(str(p) for p in Path(__file__).resolve().parent.glob("*.json"))
    if not args:
        print("ERROR: no dashboard JSON files to validate", file=sys.stderr)
        sys.exit(2)
    all_errs = []
    for path in args:
        all_errs += check(path, forbid_5m)
    if all_errs:
        print("\n".join(all_errs))
        print(f"\nFAIL: {len(all_errs)} violation(s)")
        sys.exit(1)
    print(f"OK: {len(args)} dashboard(s) passed")


if __name__ == "__main__":
    main()
