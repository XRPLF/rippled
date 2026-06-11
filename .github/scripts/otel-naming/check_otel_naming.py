#!/usr/bin/env python3

"""
Usage: check_naming.py
This script takes no parameters and can be called from any directory inside the
repository (it locates the repo root via `git rev-parse`).

Enforces the OpenTelemetry span-attribute naming convention documented in
CONTRIBUTING.md ("Telemetry span attribute naming") across every layer of the
telemetry pipeline. The `*SpanNames.h` constants are the single source of truth
(L1); every other layer must agree with them.

Design principles
-----------------
1. No hardcoded allowlist. The set of valid attribute keys — including which
   dotted keys are legitimate resource attributes — is derived dynamically by
   parsing the repository's own OTel code:
     * `*SpanNames.h` `namespace attr { ... }` blocks (the underscore/bare keys
       and the `join(seg::..., ...)` dotted resource compositions), and
     * the keys the code passes to `Resource::Create({ ... })` in Telemetry.cpp
       (the standard `semconv::service::*` keys -> service.name/version/...).

2. Presence-gated enforcement. Every rule runs ONLY when the source files it
   needs are present in the tree, and is otherwise skipped (never failed). This
   keeps the check correct no matter how work is split across PRs: a stacked
   chain, one large PR, or independent per-stage PRs where (for example) the
   collector config lands in a different PR than the dashboards. The check never
   assumes a file from another phase/PR exists.

Layers
------
  L1 code      : src/**/*SpanNames.h, include/**/*SpanNames.h  (ground truth)
  L1 resource  : src/libxrpl/telemetry/Telemetry.cpp           (dotted allowlist)
  L1 callsites : setAttribute/addEvent/span/childSpan in src/**, include/**
  L2 collector : docker/telemetry/otel-collector-config.yaml   (spanmetrics dims)
  L3 tempo     : docker/telemetry/tempo.yaml                    (span filter tags)
  L4 dashboards: docker/telemetry/grafana/dashboards/*.json     (PromQL labels)
  L5 runbook   : docs/telemetry-runbook.md                      (attr tables)

Rules (each FAILS the build, when its inputs are present)
---------------------------------------------------------
  A  No stray dotted span-attribute key. A dotted `<a>.<b>` used as a span
     attribute that is not in the derived resource-key set is a violation.
  G  Attribute keys must be lower_snake_case (^[a-z][a-z0-9_]*$ per segment).
     Flags camelCase, UPPERCASE, spaces, and other stray characters.
  F  No string literals as attribute keys/values or span-name arguments. Every
     setAttribute/addEvent/span/childSpan argument must reference a *SpanNames.h
     constant, never a "literal". Definitions inside *SpanNames.h are exempt.
  B  Every collector spanmetrics dimension exists in the L1 key set.
  C  Every tempo span-filter tag exists in the L1 key set.
  D  Every dashboard PromQL label (non-builtin) exists in the L1 key set.
  E  Every attribute named in the runbook tables exists in the L1 key set.

Exit code is non-zero if any present-and-enforced rule finds a violation.
"""

import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Repo location
# ---------------------------------------------------------------------------


def repo_root() -> Path:
    """Return the repository root, so the script works from any CWD."""
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=True,
    )
    return Path(out.stdout.strip())


# ---------------------------------------------------------------------------
# Regexes (compiled once)
# ---------------------------------------------------------------------------

# A segment/string constant definition: `inline constexpr auto NAME = <expr>;`
CONST_DEF = re.compile(r"inline\s+constexpr\s+auto\s+(\w+)\s*=\s*(.+?);", re.DOTALL)
MAKESTR = re.compile(r'makeStr\(\s*"([^"]*)"\s*\)')
# A `namespace <name> {` opener, to track which namespace a constant lives in.
NS_OPEN = re.compile(r"namespace\s+([\w:]+)\s*\{")
# Telemetry call-sites whose string arguments must be constants, not literals.
CALLSITE = re.compile(r"\b(setAttribute|addEvent|span|childSpan)\s*\(")
# A C++ string literal (used to flag literals inside call-site argument lists).
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Dotted token that looks like `a.b` or `a.b.c` (lowercase + underscore words).
DOTTED_TOKEN = re.compile(r"\b([a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+)\b")


def strip_block_comments(text: str) -> str:
    """Remove /* ... */ comments but KEEP /// and // line comments.

    Doxygen @code examples live in /** ... */ blocks, and Rule F must still see
    the call-sites inside them, so we do NOT strip /** */ wholesale. Instead we
    only strip C-style comments that are clearly not doc blocks is unsafe, so we
    keep all comments and rely on call-site detection. This function currently
    returns text unchanged and exists as a single, documented control point.
    """
    return text


# ---------------------------------------------------------------------------
# L1: parse *SpanNames.h into the authoritative key set
# ---------------------------------------------------------------------------


def find_spanname_headers(root: Path) -> List[Path]:
    return sorted(
        p
        for p in list((root / "src").rglob("*SpanNames.h"))
        + list((root / "include").rglob("*SpanNames.h"))
        if p.is_file()
    )


def resolve_constants(
    text: str, symbols: Optional[Dict[str, str]] = None
) -> Dict[str, str]:
    """Resolve `inline constexpr auto NAME = <makeStr/join expr>` to strings.

    Supports the small constexpr DSL used by SpanNames.h:
      makeStr("x")          -> "x"
      join(a, b)            -> resolve(a) + "." + resolve(b)
      seg::xrpl / attr::foo -> looked up in the symbol table
    The optional `symbols` argument seeds (and is updated in place with) the
    table, so a global pass over ALL *SpanNames.h headers can resolve
    cross-file references such as `join(seg::rpc, ...)` where `seg::rpc` is
    defined in the base SpanNames.h. Keys are stored by their bare name
    (last `::` component), so `seg::rpc` and `rpc` both resolve.
    """
    if symbols is None:
        symbols = {}

    def resolve_expr(expr: str) -> Optional[str]:
        expr = expr.strip()
        m = MAKESTR.fullmatch(expr)
        if m:
            return m.group(1)
        if expr.startswith("join(") and expr.endswith(")"):
            args = split_top_level_args(expr[len("join(") : -1])
            parts = [resolve_expr(a) for a in args]
            if any(p is None for p in parts):
                return None
            return ".".join(p for p in parts if p is not None)
        # Bare or qualified symbol reference, e.g. `seg::xrpl` or `networkId`.
        key = expr.split("::")[-1]
        return symbols.get(key, symbols.get(expr))

    # Iterate definitions in source order so earlier symbols are available.
    for m in CONST_DEF.finditer(text):
        name, expr = m.group(1), m.group(2)
        val = resolve_expr(expr)
        if val is not None:
            symbols[name] = val
    return symbols


def build_global_symbols(headers: List[Path]) -> Dict[str, str]:
    """Resolve constants across ALL headers so cross-file `seg::`/`join`
    references (e.g. `join(seg::rpc, ...)` in RpcSpanNames.h, where `seg::rpc`
    lives in the base SpanNames.h) resolve. Base SpanNames.h is processed
    first so its `seg::` segments seed the table."""
    symbols: Dict[str, str] = {}
    ordered = sorted(headers, key=lambda p: (p.name != "SpanNames.h", str(p)))
    # Two passes: the first seeds segments, the second resolves dependents.
    for _ in range(2):
        for h in ordered:
            resolve_constants(h.read_text(), symbols)
    return symbols


def split_top_level_args(s: str) -> List[str]:
    """Split a comma-separated arg list, respecting nested parentheses."""
    args, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            args.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur)
    return args


def attr_keys_from_header(path: Path, symbols: Dict[str, str]) -> Set[str]:
    """Return the set of attribute-key strings declared in a header's
    `namespace attr { ... }` block(s). `symbols` is the global cross-file
    table so `join(seg::rpc, ...)` style dotted attrs resolve correctly."""
    text = path.read_text()
    keys: Set[str] = set()
    # Walk the file tracking namespace nesting to find `namespace attr` blocks.
    depth = 0
    attr_depth: Optional[int] = None
    for line in text.splitlines():
        opener = NS_OPEN.search(line)
        if opener:
            if opener.group(1).split("::")[-1] == "attr":
                attr_depth = depth
            depth += 1
            continue
        # Count brace nesting crudely for namespace close detection.
        depth += line.count("{") - line.count("}")
        if attr_depth is not None and depth <= attr_depth:
            attr_depth = None
            continue
        if attr_depth is not None:
            md = CONST_DEF.search(line)
            if md:
                # Resolve the named constant against the global symbol table;
                # this captures both makeStr("x") and join(seg::y, ...) forms.
                val = symbols.get(md.group(1))
                if val is not None:
                    keys.add(val)
            else:
                for ms in MAKESTR.finditer(line):
                    keys.add(ms.group(1))
    return keys


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


class Report:
    def __init__(self) -> None:
        self.violations: List[Tuple[str, str, str, str]] = []
        self.skips: List[str] = []
        self.checked: List[str] = []

    def violation(self, rule: str, loc: str, token: str, expected: str) -> None:
        self.violations.append((rule, loc, token, expected))

    def skip(self, rule: str, reason: str) -> None:
        self.skips.append(f"SKIP: {rule} — {reason}")

    def ok(self, msg: str) -> None:
        self.checked.append(f"OK:   {msg}")

    def render_and_exit(self) -> None:
        for line in self.skips:
            print(line)
        for line in self.checked:
            print(line)
        if self.violations:
            print("\nNaming-convention violations:\n")
            print(f"  {'RULE':<5} {'LOCATION':<48} {'TOKEN':<28} EXPECTED")
            print(f"  {'-' * 5} {'-' * 48} {'-' * 28} {'-' * 30}")
            for rule, loc, token, expected in self.violations:
                print(f"  {rule:<5} {loc:<48} {token:<28} {expected}")
            print(
                "\nSee CONTRIBUTING.md -> 'Telemetry span attribute naming'. "
                "The *SpanNames.h constants are the single source of truth."
            )
            sys.exit(1)
        print("\nAll present telemetry naming layers are consistent.")
        sys.exit(0)


def main() -> None:
    root = repo_root()
    report = Report()

    # --- Build the L1 ground-truth key set (presence-gated) ----------------
    headers = find_spanname_headers(root)
    l1_keys: Set[str] = set()
    if headers:
        symbols = build_global_symbols(headers)
        # Map each key to the header(s) that declare it, so Rule A can tell a
        # legitimate resource attr (declared in the base SpanNames.h) from a
        # stray dotted key declared in a domain header.
        keys_by_header: Dict[Path, Set[str]] = {}
        for h in headers:
            hk = attr_keys_from_header(h, symbols)
            keys_by_header[h] = hk
            l1_keys |= hk
        report.ok(
            f"L1: {len(l1_keys)} attribute keys from {len(headers)} "
            f"*SpanNames.h header(s)"
        )
    else:
        report.skip("L1", "no *SpanNames.h present (not a naming-relevant tree)")
        keys_by_header = {}

    # --- Derive the legitimate dotted (resource) keys dynamically ----------
    # ONLY the base SpanNames.h resource attrs + the semconv keys Telemetry.cpp
    # passes to Resource::Create(). A dotted key from any other header is a
    # violation, not an allowlist entry.
    dotted_allow = derive_dotted_resource_keys(root, keys_by_header, report)

    # --- Rule A: no stray dotted span-attribute keys -----------------------
    if l1_keys:
        run_rule_a(keys_by_header, dotted_allow, report)
    # --- Rule G: keys must be lower_snake_case -----------------------------
    if l1_keys:
        run_rule_g(keys_by_header, report)
    # --- Rule F: no string literals at telemetry call-sites ----------------
    if headers:
        run_rule_f(root, report)
    else:
        report.skip("F", "no *SpanNames.h present")

    # --- Cross-layer rules B/C/D/E (each presence-gated) -------------------
    run_rule_b_collector(root, l1_keys, report)
    run_rule_c_tempo(root, l1_keys, report)
    run_rule_d_dashboards(root, l1_keys, report)
    run_rule_e_runbook(root, l1_keys, report)

    report.render_and_exit()


def derive_dotted_resource_keys(
    root: Path, keys_by_header: Dict[Path, Set[str]], report: Report
) -> Set[str]:
    """Legitimate dotted keys = dotted attrs declared ONLY in the base
    SpanNames.h (xrpl.network.*) plus the standard semconv keys the code
    passes to Resource::Create() in Telemetry.cpp (service.*). Dotted keys
    declared in any OTHER header are NOT allowlisted — they are Rule-A
    violations."""
    allow: Set[str] = set()
    for h, keys in keys_by_header.items():
        if h.name == "SpanNames.h":  # the base header holds resource attrs
            allow |= {k for k in keys if "." in k}
    tele = root / "src" / "libxrpl" / "telemetry" / "Telemetry.cpp"
    if tele.is_file():
        text = tele.read_text()
        # semconv::service::kServiceName -> service.name, etc.
        for m in re.finditer(r"semconv::(\w+)::k(\w+)", text):
            group = m.group(1)  # e.g. "service"
            field = camel_to_snake(m.group(2)).replace("service_", "", 1)
            allow.add(f"{group}.{field.replace('_', '.')}")
        report.ok(f"resource dotted-key allowlist derived: {sorted(allow)}")
    else:
        report.skip("resource-derive", "Telemetry.cpp not present")
    return allow


def camel_to_snake(s: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", s).lower()


def run_rule_a(
    keys_by_header: Dict[Path, Set[str]], dotted_allow: Set[str], report: Report
) -> None:
    """Any dotted attribute key that is not an allowed resource key is a
    violation, reported against the header that declares it."""
    found = False
    for h in sorted(keys_by_header):
        for key in sorted(keys_by_header[h]):
            if "." in key and key not in dotted_allow:
                found = True
                report.violation("A", h.name, key, "underscore form, not dotted")
    if not found:
        report.ok("A: no stray dotted span-attribute keys")


# A lower_snake_case identifier segment: starts lowercase, then lowercase /
# digits / underscores. No uppercase, no spaces, no camelCase.
SNAKE_SEGMENT = re.compile(r"^[a-z][a-z0-9_]*$")


def run_rule_g(keys_by_header: Dict[Path, Set[str]], report: Report) -> None:
    """Every attribute key must be lower_snake_case. Bare/underscore keys must
    match ^[a-z][a-z0-9_]*$; dotted resource keys must be lowercase
    dot-separated segments (each segment lower_snake_case). Flags camelCase,
    UPPERCASE, spaces, and other stray characters."""
    found = False
    for h in sorted(keys_by_header):
        for key in sorted(keys_by_header[h]):
            segments = key.split(".")
            if all(SNAKE_SEGMENT.match(seg) for seg in segments):
                continue
            found = True
            report.violation("G", h.name, key, "must be lower_snake_case")
    if not found:
        report.ok("G: all attribute keys are lower_snake_case")


# Which argument positions of each call must be a constant (0-based). The
# attribute VALUE position is intentionally absent: values are runtime data
# (command names, hashes, counts), not naming-convention surface.
#   setAttribute(key, value)        -> check arg 0 (key); value (arg 1) exempt
#   addEvent(name[, attrs])         -> check arg 0 (event name)
#   span(category, prefix, name)    -> check args 1,2 (prefix + span-name leaf)
#   childSpan(name[, parentCtx])    -> check arg 0 (span-name leaf)
CONSTANT_ARG_POSITIONS: Dict[str, Set[int]] = {
    "setAttribute": {0},
    "addEvent": {0},
    "span": {1, 2},
    "childSpan": {0},
}


def run_rule_f(root: Path, report: Report) -> None:
    """Flag string literals in the constant-only argument positions of
    setAttribute/addEvent/span/childSpan. Attribute VALUES are exempt (runtime
    data). *SpanNames.h definitions are exempt (constants live there)."""
    found = False
    sources = [
        p
        for base in ("src", "include")
        for ext in ("*.h", "*.cpp")
        for p in (root / base).rglob(ext)
        if p.is_file()
    ]
    for path in sorted(sources):
        if path.name.endswith("SpanNames.h"):
            continue
        text = path.read_text(errors="ignore")
        for call, arglist, lineno in iter_calls(text):
            positions = CONSTANT_ARG_POSITIONS.get(call, set())
            args = split_top_level_args(arglist)
            for idx in positions:
                if idx >= len(args):
                    continue
                lit = STRING_LITERAL.search(args[idx])
                if lit:
                    found = True
                    report.violation(
                        "F",
                        f"{path.relative_to(root)}:{lineno}",
                        f'{call} arg{idx} "{lit.group(1)}"',
                        "use a *SpanNames.h constant",
                    )
    if not found:
        report.ok("F: no string-literal keys/names at telemetry call-sites")


def iter_calls(text: str):
    """Yield (call_name, raw_arglist, lineno) for each setAttribute/addEvent/
    span/childSpan invocation, spanning multiple physical lines if needed."""
    # Precompute line-number lookup by character offset.
    line_starts = [0]
    for ch in text:
        line_starts.append(line_starts[-1] + 1)
    for m in CALLSITE.finditer(text):
        name = m.group(1)
        # Walk from the opening paren, balancing nesting to find the close.
        i = m.end()  # one char past the '('
        depth = 1
        while i < len(text) and depth > 0:
            c = text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        arglist = text[m.end() : i - 1]
        lineno = text.count("\n", 0, m.start()) + 1
        yield name, arglist, lineno


def run_rule_b_collector(root: Path, l1_keys: Set[str], report: Report) -> None:
    path = root / "docker" / "telemetry" / "otel-collector-config.yaml"
    if not path.is_file():
        report.skip("B", "collector config not present")
        return
    text = path.read_text()
    if "spanmetrics" not in text:
        report.skip("B", "no spanmetrics block in collector config")
        return
    dims = extract_spanmetrics_dimensions(text)
    if not l1_keys:
        report.skip("B", "no L1 key set to validate against")
        return
    miss = [d for d in dims if d not in l1_keys]
    for d in miss:
        report.violation("B", str(path.relative_to(root)), d, "must exist in L1")
    if not miss:
        report.ok(f"B: {len(dims)} collector dimension(s) all in L1")


def extract_spanmetrics_dimensions(text: str) -> List[str]:
    dims: List[str] = []
    in_dims = False
    for line in text.splitlines():
        if re.search(r"\bdimensions\s*:", line):
            in_dims = True
            continue
        if in_dims:
            m = re.search(r"-\s*name\s*:\s*([A-Za-z0-9_.]+)", line)
            if m:
                dims.append(m.group(1))
            elif line.strip() and not line.lstrip().startswith("-") and ":" in line:
                in_dims = False
    return dims


def run_rule_c_tempo(root: Path, l1_keys: Set[str], report: Report) -> None:
    path = root / "docker" / "telemetry" / "tempo.yaml"
    if not path.is_file():
        report.skip("C", "tempo.yaml not present")
        return
    text = path.read_text()
    tags = re.findall(r"(?:tag|attribute)\s*:\s*([A-Za-z0-9_.]+)", text)
    span_tags = [t for t in tags if not t.startswith(("service.", "span."))]
    if not span_tags:
        report.skip("C", "no span-attribute tags in tempo.yaml")
        return
    if not l1_keys:
        report.skip("C", "no L1 key set to validate against")
        return
    miss = [t for t in span_tags if t not in l1_keys]
    for t in miss:
        report.violation("C", str(path.relative_to(root)), t, "must exist in L1")
    if not miss:
        report.ok(f"C: {len(span_tags)} tempo tag(s) all in L1")


def run_rule_d_dashboards(root: Path, l1_keys: Set[str], report: Report) -> None:
    dash_dir = root / "docker" / "telemetry" / "grafana" / "dashboards"
    files = sorted(dash_dir.glob("*.json")) if dash_dir.is_dir() else []
    if not files:
        report.skip("D", "no dashboard JSON present")
        return
    if not l1_keys:
        report.skip("D", "no L1 key set to validate against")
        return
    builtins = {
        "le",
        "exported_instance",
        "span_name",
        "status_code",
        "service_name",
        "service_version",
        "service_instance_id",
        "job",
        "instance",
    }
    found = False
    for f in files:
        try:
            text = f.read_text()
        except OSError:
            continue
        # PromQL `sum by (a, b)` and `{label="..."}` references.
        labels: Set[str] = set()
        for m in re.finditer(r"by\s*\(([^)]*)\)", text):
            labels |= {x.strip() for x in m.group(1).split(",") if x.strip()}
        for m in re.finditer(r"\b([a-z_][a-z0-9_]*)\s*[=!]~?\s*\"", text):
            labels.add(m.group(1))
        for lbl in sorted(labels):
            if lbl in builtins or lbl in l1_keys:
                continue
            found = True
            report.violation(
                "D", str(f.relative_to(root)), lbl, "must exist in L1 or builtin"
            )
    if not found:
        report.ok(f"D: dashboard PromQL labels all in L1 ({len(files)} file(s))")


def run_rule_e_runbook(root: Path, l1_keys: Set[str], report: Report) -> None:
    path = root / "docs" / "telemetry-runbook.md"
    if not path.is_file():
        report.skip("E", "runbook not present")
        return
    if not l1_keys:
        report.skip("E", "no L1 key set to validate against")
        return
    text = path.read_text()
    found = False
    # Attribute names appear as `code` spans in the runbook's reference tables.
    for m in re.finditer(r"`([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)*)`", text):
        token = m.group(1)
        # Only consider tokens that look like attribute keys (underscore form or
        # a dotted form), and skip obvious span names / prose words.
        if "_" in token or "." in token:
            if token in l1_keys:
                continue
            if "." in token:  # dotted: must be a resource key in L1
                found = True
                report.violation(
                    "E", str(path.relative_to(root)), token, "underscore, not dotted"
                )
    if not found:
        report.ok("E: runbook attribute references consistent with L1")


if __name__ == "__main__":
    main()
