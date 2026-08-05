#!/usr/bin/env python3
"""Dashboard lint: cumulative metrics rate()-wrapped; tier filters; sane panel grid."""

import json, re, sys

GRID_COLUMNS = 24

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


def check_layout(path, dash):
    """Duplicate panel ids and grid collisions -- both break Grafana's loader.

    Grafana keys panels by id when it builds the dashboard model, so two panels
    sharing an id make the load non-deterministic. Overlapping gridPos rectangles
    have no valid layout. Only top-level panels are checked: panels nested inside
    a collapsed row do not occupy the outer grid.
    """
    errs = []
    top = dash.get("panels", []) or []

    seen_ids = {}
    for p in top:
        pid = p.get("id")
        if pid is None:
            continue
        if pid in seen_ids:
            errs.append(
                f"{path}: duplicate panel id {pid}: "
                f"[{seen_ids[pid]}] and [{p.get('title', '<untitled>')}]"
            )
        else:
            seen_ids[pid] = p.get("title", "<untitled>")

    # Mark every grid cell each panel covers; a second claim on a cell is a collision.
    owner = {}
    for p in top:
        g = p.get("gridPos") or {}
        x, y = g.get("x", 0), g.get("y", 0)
        w, h = g.get("w", 0), g.get("h", 0)
        title = p.get("title", "<untitled>")
        if x + w > GRID_COLUMNS:
            errs.append(
                f"{path} [{title}]: spans past the {GRID_COLUMNS}-column grid (x={x}, w={w})"
            )
        clashed = set()
        for cy in range(y, y + h):
            for cx in range(x, min(x + w, GRID_COLUMNS)):
                prev = owner.get((cy, cx))
                if prev is None:
                    owner[(cy, cx)] = title
                elif prev not in clashed:
                    clashed.add(prev)
                    errs.append(
                        f"{path} [{title}]: grid overlap with [{prev}] at y={cy} x={cx}"
                    )
    return errs


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
    errs += check_layout(path, dash)
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
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    forbid_5m = "--no-5m" in sys.argv
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
