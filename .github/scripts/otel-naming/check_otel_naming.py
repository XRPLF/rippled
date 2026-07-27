#!/usr/bin/env python3

"""
Usage: check_otel_naming.py
This script takes no parameters and can be called from any directory inside the
repository (it locates the repo root via `git rev-parse`).

Enforces the OpenTelemetry naming conventions documented in CONTRIBUTING.md
("Telemetry span attribute naming" and "Telemetry metric naming") across every
layer of the telemetry pipeline. The `*SpanNames.h` constants are the single
source of truth for span attributes (L1) and the `*MetricNames.h` constants for
metric names and label keys (L1-metrics); every other layer must agree with
them.

Design principles
-----------------
1. No hardcoded allowlist. The set of valid attribute keys — including which
   dotted keys are legitimate resource attributes — is derived dynamically by
   parsing the repository's own OTel code:
     * `*SpanNames.h` `namespace attr { ... }` blocks (the underscore/bare keys
       and the `join(seg::..., ...)` dotted resource compositions), and
     * the keys the code passes to `Resource::Create({ ... })` in Telemetry.cpp
       (the standard `semconv::service::*` keys -> service.name/version/...).
   The one narrow, explicit exception is EXTERNAL_INFRA_LABELS (Rules D & E):
   identity labels stamped by infrastructure outside this repo's OTel code
   (the perf-iac harness), which by definition have no source in-tree to
   derive from. Kept separate from the generic Prometheus/Grafana builtins
   set so the exception stays visible rather than blending into "things
   every OTel setup has". perf-iac's alloy pipeline stamps each identity at
   two layers -- dotted on the OTel resource attribute (xrpl.work.item/
   .branch/.node.role, checked by Rule E) and underscore on the derived
   Prometheus metric-datapoint label (xrpl_work_item/_branch/_node_role,
   checked by Rule D) -- so both forms are exempt from the same constant.

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
  L1 callsites : setAttribute/addEvent/span/rootSpan/childSpan in src/**,
                 include/**
  L2 collector : docker/telemetry/otel-collector-config.yaml   (spanmetrics dims)
  L3 tempo     : docker/telemetry/tempo.yaml                    (span filter tags)
  L4 dashboards: docker/telemetry/grafana/dashboards/*.json     (PromQL labels)
  L5 runbook   : docs/telemetry-runbook.md                      (attr tables)
  L6 metrics   : MetricsRegistry.cpp instrument labels          (native-metric
                 label keys, a valid dashboard-label source besides L1)
  L1-metrics   : src/**/*MetricNames.h, include/**/*MetricNames.h (ground truth
                 for metric instrument names, label keys and bounded values)
  L7 workload  : docker/telemetry/workload/expected_metrics.json  (asserted
                 metric names)

Rules (each FAILS the build, when its inputs are present)
---------------------------------------------------------
  A  No stray dotted span-attribute key. A dotted `<a>.<b>` used as a span
     attribute that is not in the derived resource-key set is a violation.
  G  Attribute keys must be lower_snake_case (^[a-z][a-z0-9_]*$ per segment).
     Flags camelCase, UPPERCASE, spaces, and other stray characters.
  F  No string literals as attribute keys or span-name arguments. The
     setAttribute/addEvent key and the span/rootSpan/childSpan prefix/name args
     must reference a *SpanNames.h constant, never a "literal". Attribute VALUES
     are exempt (runtime data). Definitions inside *SpanNames.h are exempt, and
     test files are exempt (they pass arbitrary literals to exercise the API).
  B  Every collector spanmetrics dimension exists in the L1 key set.
  C  Every tempo span-filter tag exists in the L1 key set.
  D  Every dashboard label resolves to an L1 span attribute, an L6
     native-metric label, or a builtin. TraceQL `span.`/`resource.` scope
     prefixes are stripped before the L1 lookup.
  E  No dotted `xrpl.<domain>.<field>` attribute key in the runbook (only the
     L1 resource attrs xrpl.network.* and the EXTERNAL_INFRA_LABELS dotted
     form -- xrpl.work.item/.branch/.node.role -- may be dotted). Span names,
     filenames,
     OTel-standard keys, and metric labels are not flagged.
  I  No string literals as METRIC instrument names or label keys. The mirror of
     Rule F for metrics: the name passed to an XRPL_METRIC_* macro or to a
     meter->Create* factory, and the label KEYS in its label set, must reference
     a *MetricNames.h constant. Label VALUES are exempt (runtime data), as are
     the descriptions, *MetricNames.h itself, MetricMacros.h, and test files.
     Scoped by metric FAMILY (first underscore segment) so the metric surface
     can be converted subsystem by subsystem -- declaring a constant for a
     family opts that family into enforcement. See Rule L.
  J  Metric instrument names follow the naming and suffix conventions:
     lower_snake_case, no xrpld_/xrpl_ prefix (the exporter adds it), a counter
     ends in _total, a histogram ends in _us/_ms/_seconds, and a gauge does not
     end in _total. The instrument KIND is read from the emit site, never
     guessed from words in the name.
  K  Every metric named in expected_metrics.json resolves to a *MetricNames.h
     constant, so a rename in code cannot leave the workload validator
     asserting a name nothing emits. PromQL selectors and exporter-appended
     histogram suffixes are normalized away first; groups fed by another emit
     path (statsd_*, spanmetrics) are out of scope.

Warnings (printed, but do NOT fail the build)
----------------------------------------------
  H  A constant referenced at a telemetry call-site is not defined in any
     *SpanNames.h. Span constants should live in the corresponding
     *SpanNames.h (single source of truth); defining one in-place bypasses the
     naming rules. A warning (not a failure) because the argument may instead
     be a legitimately dynamic local (e.g. a computed span-name leaf).
  L  A literal metric name in a family that has no *MetricNames.h constants
     yet. Rule I's ratchet defers these rather than failing the build on the
     whole pre-existing metric surface at once; the warning is what keeps the
     outstanding conversion work visible instead of silently accepted.

Exit code is non-zero if any present-and-enforced rule finds a violation.
Warnings never change the exit code.
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
    """Return the repository root, so the script works from any CWD.

    Exits with a readable message (not a traceback) if git is unavailable or the
    CWD is outside a repository."""
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(
            "error: check_otel_naming.py must be run inside the git repository.",
            file=sys.stderr,
        )
        sys.exit(2)
    return Path(out.stdout.strip())


def read_source(path: Path) -> str:
    """Read a file as UTF-8, tolerating stray non-UTF-8 bytes rather than
    crashing the whole check on one bad byte."""
    return path.read_text(encoding="utf-8", errors="ignore")


# ---------------------------------------------------------------------------
# Regexes (compiled once)
# ---------------------------------------------------------------------------

# A segment/string constant definition: `inline constexpr auto NAME = <expr>;`
CONST_DEF = re.compile(r"inline\s+constexpr\s+auto\s+(\w+)\s*=\s*(.+?);", re.DOTALL)
MAKESTR = re.compile(r'makeStr\(\s*"([^"]*)"\s*\)')
# A `namespace <name> {` opener, to track which namespace a constant lives in.
NS_OPEN = re.compile(r"namespace\s+([\w:]+)\s*\{")
# A `using ::a::b::field;` re-export inside an attr block; captures the leaf.
USING_DECL = re.compile(r"using\s+(?:::)?[\w:]*::(\w+)\s*;")
# Telemetry call-sites whose string arguments must be constants, not literals.
# Require a receiver so we match real SpanGuard calls, not std::span / a math
# `span(...)` / a bare method declaration:
#   - `SpanGuard::span(` / `SpanGuard::rootSpan(` / `SpanGuard::childSpan(`
#     (static factories)
#   - `<obj>.span(` / `<obj>->setAttribute(` etc.   (member call)
# `span`/`rootSpan`/`childSpan` additionally require the `SpanGuard`/`.`/`->`
# receiver; `setAttribute`/`addEvent` only ever exist on a guard, so a `.`/`->`
# suffices. `rootSpan` shares `span`'s (cat, prefix, name) signature.
CALLSITE = re.compile(
    r"(?:SpanGuard::|\.|->)\s*(setAttribute|addEvent|span|rootSpan|childSpan)\s*\("
)
# A C++ string literal (used to flag literals inside call-site argument lists).
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# A C++ line comment (`//` ... end of line) and a block comment (`/* ... */`).
LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
# A TraceQL scope prefix on a label (`span.`, `resource.`, `event.`, etc.).
# Dashboards reference span attributes in TraceQL as `span.<attr>`; the bare
# attribute is what must exist in L1, so strip the scope before validating.
TRACEQL_SCOPE = re.compile(r"^(?:span|resource|event|link|instrumentation_scope)\.")
# An OTel metric label key as emitted in C++: `Add(.., {{"label", ...}})` /
# `{{"label", value}}` instrument calls in MetricsRegistry.
#
# Two patterns are needed because a label set is a nested initializer list:
# `{{"a", x}, {"b", y}}`. The FIRST label is preceded by the doubled brace that
# opens both the set and the pair, while every SUBSEQUENT label is preceded by
# `}, {` closing the previous pair and opening the next. Matching only the
# doubled-brace form would derive just the first label of every multi-label
# instrument, silently under-deriving the L6 key set and making Rule D reject a
# dashboard that queries a label the code genuinely emits.
METRIC_LABEL = re.compile(r'\{\{\s*"([a-z_][a-z0-9_]*)"\s*,')
METRIC_LABEL_NEXT = re.compile(r'\}\s*,\s*\{\s*"([a-z_][a-z0-9_]*)"\s*,')


def strip_comments(text: str) -> str:
    """Remove C/C++ `//` line comments and `/* ... */` block comments.

    Used only for L1 attribute-key extraction so that a commented-out or
    illustrative `makeStr("...")` inside a `namespace attr` block does not leak
    into the authoritative key set. Rule F deliberately does NOT strip comments
    — it must still see `@code` doc-comment examples so their call-site
    arguments are held to the constant-only convention.

    String literals are not specially handled; a `//` or `/*` appearing inside a
    string is vanishingly rare in the *SpanNames.h headers and would at worst
    drop a constant from L1 (a conservative direction).
    """
    text = BLOCK_COMMENT.sub("", text)
    text = LINE_COMMENT.sub("", text)
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
    # Comments are stripped so a commented-out constant cannot seed the table.
    for _ in range(2):
        for h in ordered:
            resolve_constants(strip_comments(read_source(h)), symbols)
    return symbols


def split_top_level_args(s: str) -> List[str]:
    """Split a comma-separated arg list, respecting nested parentheses and
    ignoring parens/commas that appear inside a "string literal" (so a value
    like `setAttribute(k, ",")` does not get mis-split)."""
    args, depth, cur = [], 0, ""
    in_str = False
    escaped = False
    for ch in s:
        if in_str:
            cur += ch
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            cur += ch
        elif ch == "(":
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


def attr_namespace_spans(text: str) -> List[str]:
    """Return the source text of each `namespace attr { ... }` block in `text`.

    Brace-matched over the whole (comment-stripped) text, so a definition that
    wraps across several physical lines is contained in one span. Nested braces
    inside the block are balanced correctly."""
    spans: List[str] = []
    for opener in NS_OPEN.finditer(text):
        if opener.group(1).split("::")[-1] != "attr":
            continue
        # Walk from the opening brace, balancing nesting to the matching close.
        i = opener.end()  # one char past the namespace's `{`
        depth = 1
        start = i
        while i < len(text) and depth > 0:
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            i += 1
        spans.append(text[start : i - 1])
    return spans


def attr_keys_from_header(path: Path, symbols: Dict[str, str]) -> Set[str]:
    """Return the set of attribute-key strings declared in a header's
    `namespace attr { ... }` block(s). `symbols` is the global cross-file
    table, used ONLY to seed `seg::`/segment references for `join(...)`
    resolution — never to look up an attr constant's value.

    A constant DEFINED in this header is resolved against this header's OWN
    text, so two headers that each define a same-named constant (e.g. the base
    `attr::ledgerHash = xrpl.ledger.hash` and consensus
    `attr::ledgerHash = ledger_hash`) each report their real wire key. The
    global table is keyed by bare name and would otherwise let a later header
    clobber an earlier one, erasing the real key from L1 (a Rule-A blind spot).
    A `using`-re-export, by contrast, imports a constant defined elsewhere, so
    it is resolved against the global table.

    Comments are stripped first (a commented constant must not enter L1), and
    each attr block is brace-matched over the whole text so multi-line
    `inline constexpr auto NAME = join(\\n  ...);` definitions are captured."""
    text = strip_comments(read_source(path))
    # Local table: the global segments/symbols seed cross-file `join` parts,
    # then this header's own definitions overwrite any same-named global entry
    # so a locally-defined attr resolves to ITS value, not another header's.
    local = dict(symbols)
    resolve_constants(text, local)
    keys: Set[str] = set()
    for block in attr_namespace_spans(text):
        for md in CONST_DEF.finditer(block):
            # Resolve a locally-defined constant against the LOCAL table; this
            # captures makeStr("x") and join(seg::y, ...) with the header's own
            # value, immune to cross-header bare-name collisions.
            val = local.get(md.group(1))
            if val is not None:
                keys.add(val)
        # `using ::ns::attr::field;` re-exports a constant defined in ANOTHER
        # header (e.g. PeerSpanNames imports the base ledgerHash). Resolve the
        # imported name against the global table.
        for um in USING_DECL.finditer(block):
            val = symbols.get(um.group(1))
            if val is not None:
                keys.add(val)
    return keys


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


class Report:
    def __init__(self) -> None:
        self.violations: List[Tuple[str, str, str, str]] = []
        self.warnings: List[Tuple[str, str, str, str]] = []
        self.skips: List[str] = []
        self.checked: List[str] = []

    def violation(self, rule: str, loc: str, token: str, expected: str) -> None:
        self.violations.append((rule, loc, token, expected))

    def warning(self, rule: str, loc: str, token: str, note: str) -> None:
        """A non-fatal finding: printed, but does not fail the build. Used where
        the script cannot be certain a finding is wrong (e.g. a constant used at
        a call-site that is not defined in any *SpanNames.h — it might be a
        misplaced constant, or a legitimately dynamic value)."""
        self.warnings.append((rule, loc, token, note))

    def skip(self, rule: str, reason: str) -> None:
        self.skips.append(f"SKIP: {rule} — {reason}")

    def ok(self, msg: str) -> None:
        self.checked.append(f"OK:   {msg}")

    def render_and_exit(self) -> None:
        for line in self.skips:
            print(line)
        for line in self.checked:
            print(line)
        if self.warnings:
            print("\nNaming-convention warnings (non-fatal):\n")
            print(f"  {'RULE':<5} {'LOCATION':<48} {'TOKEN':<28} NOTE")
            print(f"  {'-' * 5} {'-' * 48} {'-' * 28} {'-' * 30}")
            for rule, loc, token, note in self.warnings:
                print(f"  {rule:<5} {loc:<48} {token:<28} {note}")
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
    # ONLY the keys actually passed to Resource::Create() in Telemetry.cpp
    # (semconv service.* + the attr:: constants set there, e.g. xrpl.network.*).
    # A dotted key declared in a header but NOT set as a resource attr is a
    # Rule-A violation, not an allowlist entry.
    resource_symbols = symbols if headers else {}
    dotted_allow = derive_dotted_resource_keys(root, resource_symbols, report)

    # --- Rule A: no stray dotted span-attribute keys -----------------------
    if l1_keys:
        run_rule_a(keys_by_header, dotted_allow, report)
    # --- Rule G: keys must be lower_snake_case -----------------------------
    if l1_keys:
        run_rule_g(keys_by_header, report)
    # --- Rule F (+ Rule H): scan telemetry call-sites ----------------------
    # Runs UNCONDITIONALLY: Rule F is a purely syntactic check (is this argument
    # a literal?) and does not need the L1 key set, so a code path that uses
    # SpanGuard::span/setAttribute directly without ever defining a *SpanNames.h
    # is still caught. Rule H (warning) additionally flags constant references
    # not defined in any *SpanNames.h.
    header_symbols = spanname_symbol_names(headers)
    run_rule_f(root, report, header_symbols)

    # --- Cross-layer rules B/C/D/E (each presence-gated) -------------------
    # L6 native-metric labels: span attributes are not the only valid dashboard
    # labels — the MetricsRegistry emits OTel metrics whose label keys are an
    # additional source of truth. Derive them dynamically (same principle as L1)
    # so dashboards may reference them without tripping Rule D.
    metric_labels = metric_label_names(root)
    run_rule_b_collector(root, l1_keys, report)
    run_rule_c_tempo(root, l1_keys, report)
    run_rule_d_dashboards(root, l1_keys, metric_labels, report)
    run_rule_e_runbook(root, l1_keys, report)

    # --- Metric rules I/J/K --------------------------------------------------
    # The metric-name counterpart of the span rules above. Rule I is the mirror
    # of Rule F (no literals at an emit site) and, like it, runs
    # unconditionally because it is purely syntactic. Rules J and K are
    # presence-gated on *MetricNames.h existing.
    l1_metric_names, l1_metric_labels, _ = metric_constants(root)
    if find_metricname_headers(root):
        report.ok(
            f"L1-metrics: {len(l1_metric_names)} instrument name(s) and "
            f"{len(l1_metric_labels)} label key(s) from "
            f"{len(find_metricname_headers(root))} *MetricNames.h header(s)"
        )
    else:
        report.skip("L1-metrics", "no *MetricNames.h present")
    run_rule_i_metric_literals(root, report)
    run_rule_j_metric_suffixes(root, report)
    run_rule_k_expected_metrics(root, report)

    report.render_and_exit()


def resource_create_block(text: str) -> str:
    """Return the text inside the first `Resource::Create({ ... })` argument
    list, brace-matched so nested `{key, value}` initializers are contained.
    Empty string if the call is absent."""
    m = re.search(r"Resource::Create\(\s*\{", text)
    if not m:
        return ""
    i = m.end()  # one char past the opening `{`
    depth, start = 1, i
    while i < len(text) and depth > 0:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[start : i - 1]


def derive_dotted_resource_keys(
    root: Path, symbols: Dict[str, str], report: Report
) -> Set[str]:
    """Legitimate dotted keys = ONLY the keys the code actually sets as RESOURCE
    attributes, i.e. the entries inside Telemetry.cpp's `Resource::Create({...})`
    call: the standard semconv keys (`service.*`) plus any `attr::<name>`
    constants passed there (resolved to their wire key via the global symbol
    table, e.g. `attr::networkId` -> `xrpl.network.id`).

    A dotted key DECLARED in a `*SpanNames.h` header but NOT passed to
    Resource::Create() is a span attribute wearing the resource form — a Rule-A
    violation, never allowlisted. Deriving the allowlist from the actual
    resource call (not from "any dotted key in the base header") is what lets
    Rule A catch a stray dotted span attr such as `xrpl.ledger.hash`."""
    allow: Set[str] = set()
    tele = root / "src" / "libxrpl" / "telemetry" / "Telemetry.cpp"
    if not tele.is_file():
        report.skip("resource-derive", "Telemetry.cpp not present")
        return allow
    block = resource_create_block(read_source(tele))
    # semconv::<group>::k<CamelKey> -> the dotted OTel-standard key. The
    # CamelKey already embeds the group, e.g. service::kServiceInstanceId
    # -> service.instance.id. Split the CamelCase name into dotted lowercase
    # segments; if it does not lead with the group, prepend the group.
    for m in re.finditer(r"semconv::(\w+)::k(\w+)", block):
        group, camel = m.group(1), m.group(2)
        segments = camel_to_dotsegments(camel)
        if segments and segments[0] == group:
            allow.add(".".join(segments))
        else:
            allow.add(group + "." + ".".join(segments))
    # attr::<name> constants set as resource attrs (e.g. networkId/networkType);
    # resolve each to its wire key and allowlist only the dotted ones.
    for m in re.finditer(r"attr::(\w+)", block):
        val = symbols.get(m.group(1))
        if val is not None and "." in val:
            allow.add(val)
    report.ok(f"resource dotted-key allowlist derived: {sorted(allow)}")
    return allow


def camel_to_dotsegments(s: str) -> List[str]:
    """Split a CamelCase identifier into lowercase dot-segment parts, e.g.
    `ServiceInstanceId` -> ['service', 'instance', 'id']."""
    return [w.lower() for w in re.findall(r"[A-Z][a-z0-9]*", s)]


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
#   rootSpan(category, prefix, name)-> check args 1,2 (same signature as span)
#   childSpan(name[, parentCtx])    -> check arg 0 (span-name leaf)
CONSTANT_ARG_POSITIONS: Dict[str, Set[int]] = {
    "setAttribute": {0},
    "addEvent": {0},
    "span": {1, 2},
    "rootSpan": {1, 2},  # same signature as span(cat, prefix, name)
    "childSpan": {0},
}


def is_test_path(path: Path) -> bool:
    """True if the path is test code. Tests legitimately pass arbitrary literal
    keys/names to exercise the API mechanics, so Rule F does not apply to them.
    Matches a `test`/`tests` directory anywhere in the path (e.g. src/test/,
    src/tests/, .../detail/tests/)."""
    return any(part in ("test", "tests") for part in path.parts)


# A constant reference passed at a call-site, e.g. `rpc_span::attr::command`
# or a bare `myKey`. We capture the leaf identifier (after the last `::`).
IDENTIFIER_ARG = re.compile(r"^[\s&*]*([A-Za-z_][\w:]*)\s*$")


def spanname_symbol_names(headers: List[Path]) -> Set[str]:
    """Every `inline constexpr auto NAME = ...;` symbol defined across the
    *SpanNames.h headers, by bare name. Used by Rule H to tell whether a
    constant referenced at a call-site actually lives in a SpanNames header."""
    names: Set[str] = set()
    for h in headers:
        for m in CONST_DEF.finditer(strip_comments(read_source(h))):
            names.add(m.group(1))
    return names


def run_rule_f(root: Path, report: Report, header_symbols: Set[str]) -> None:
    """Walk every telemetry call-site (non-test, non-*SpanNames.h) and check the
    constant-only argument positions of
    setAttribute/addEvent/span/rootSpan/childSpan:

      Rule F (FAIL): a string literal in a key / span-name position. Attribute
        VALUES are exempt (runtime data).
      Rule H (WARN): a constant reference whose name is not defined in any
        *SpanNames.h. The constant should live in the corresponding
        *SpanNames.h (single source of truth); defining it in-place bypasses
        the naming rules. Warn rather than fail — the argument may instead be a
        legitimately dynamic local (e.g. a computed span-name leaf)."""
    found_f = False
    sources = [
        p
        for base in ("src", "include")
        for ext in ("*.h", "*.cpp")
        for p in (root / base).rglob(ext)
        if p.is_file()
    ]
    for path in sorted(sources):
        if path.name.endswith("SpanNames.h") or is_test_path(path):
            continue
        text = read_source(path)
        rel = path.relative_to(root)
        for call, arglist, lineno in iter_calls(text):
            positions = CONSTANT_ARG_POSITIONS.get(call, set())
            args = split_top_level_args(arglist)
            for idx in positions:
                if idx >= len(args):
                    continue
                arg = args[idx]
                lit = STRING_LITERAL.search(arg)
                if lit:
                    found_f = True
                    report.violation(
                        "F",
                        f"{rel}:{lineno}",
                        f'{call} arg{idx} "{lit.group(1)}"',
                        "use a *SpanNames.h constant",
                    )
                    continue
                # Not a literal: Rule H warns when a NAMESPACE-QUALIFIED constant
                # reference (e.g. `consensus::span::accept`) is not defined in
                # any *SpanNames.h — i.e. the constant was defined in-place
                # instead of in the proper header. We only consider qualified
                # refs (containing `::`): a bare lowercase identifier is almost
                # always a legitimately dynamic local (a computed span-name leaf
                # or attribute value), not a misplaced constant, so warning on it
                # would be noise. Standard-library types (std::...) are skipped.
                ident = IDENTIFIER_ARG.match(arg)
                if not (ident and header_symbols):
                    continue
                ref = ident.group(1)
                if "::" not in ref or ref.startswith("std::"):
                    continue
                leaf = ref.split("::")[-1]
                if leaf not in header_symbols:
                    report.warning(
                        "H",
                        f"{rel}:{lineno}",
                        f"{call} arg{idx} {ref}",
                        "not defined in any *SpanNames.h",
                    )
    if not found_f:
        report.ok("F: no string-literal keys/names at telemetry call-sites")


def iter_calls(text: str):
    """Yield (call_name, raw_arglist, lineno) for each setAttribute/addEvent/
    span/rootSpan/childSpan invocation, spanning multiple physical lines if
    needed."""
    for m in CALLSITE.finditer(text):
        name = m.group(1)
        # Walk from the opening paren, balancing nesting to find the close.
        # Parens inside a "string literal" are ignored so a value such as
        # `setAttribute(k, ")")` does not close the call early.
        i = m.end()  # one char past the '('
        depth = 1
        in_str = False
        escaped = False
        while i < len(text) and depth > 0:
            c = text[i]
            if in_str:
                if escaped:
                    escaped = False
                elif c == "\\":
                    escaped = True
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
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
    text = read_source(path)
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
    # The trace-search filter tags live in the Grafana Tempo DATASOURCE
    # provisioning file (search.filters[].{tag,scope}); the Tempo server
    # tempo.yaml has no such tags. Prefer the datasource file; fall back to the
    # server file so the rule still does something if the layout changes.
    candidates = [
        root / "docker/telemetry/grafana/provisioning/datasources/tempo.yaml",
        root / "docker/telemetry/tempo.yaml",
    ]
    path = next((p for p in candidates if p.is_file()), None)
    if path is None:
        report.skip("C", "tempo datasource provisioning not present")
        return
    if not l1_keys:
        report.skip("C", "no L1 key set to validate against")
        return
    # Pair each filter's `tag:` with its `scope:` (a few lines below it) and
    # validate only span-scope tags — resource/intrinsic tags (service.*, name,
    # status, duration) are not span attributes. Strip a TraceQL span. prefix.
    lines = read_source(path).splitlines()
    span_tags: List[str] = []
    for i, line in enumerate(lines):
        m = re.search(r"^\s*tag:\s*(\S+)", line)
        if not m:
            continue
        scope = next(
            (
                sm.group(1)
                for j in range(i, min(i + 4, len(lines)))
                for sm in [re.search(r"scope:\s*(\S+)", lines[j])]
                if sm
            ),
            "",
        )
        if scope == "span":
            span_tags.append(TRACEQL_SCOPE.sub("", m.group(1)))
    if not span_tags:
        report.skip("C", "no span-scope filter tags in tempo datasource")
        return
    miss = [t for t in span_tags if t not in l1_keys]
    for t in sorted(set(miss)):
        report.violation("C", str(path.relative_to(root)), t, "must exist in L1")
    if not miss:
        report.ok(f"C: {len(span_tags)} tempo span-filter tag(s) all in L1")


def metric_label_names(root: Path) -> Set[str]:
    """L6: OTel native-metric label keys emitted by the telemetry code, e.g.
    `counter->Add(1, {{"job_type", value}})` in MetricsRegistry.cpp. These are
    a valid source of dashboard labels distinct from span attributes (L1).

    Collects both the first label of a set and every subsequent one, so a
    multi-label instrument such as `{{"from", a}, {"to", b}}` contributes ALL
    of its keys.

    Includes the keys declared as constants in `*MetricNames.h` (L1-metrics),
    because Rule I requires call sites to reference a constant rather than a
    literal — so once a subsystem is converted, its label keys no longer appear
    as literals anywhere and a literal-only scan would under-derive the set and
    make Rule D reject a dashboard querying a label the code really emits."""
    labels: Set[str] = set()
    for base in ("src", "include"):
        for p in (root / base).rglob("*.cpp"):
            if not p.is_file():
                continue
            text = read_source(p)
            if "MetricsRegistry" not in p.name and "metric" not in text.lower():
                continue
            labels |= set(METRIC_LABEL.findall(text))
            labels |= set(METRIC_LABEL_NEXT.findall(text))
    labels |= metric_constants(root)[1]
    return labels


# ---------------------------------------------------------------------------
# L1-metrics: parse *MetricNames.h into the authoritative metric-name/label set
# ---------------------------------------------------------------------------

# A metric-name constant definition: `inline constexpr char NAME[] = "wire";`.
# The metric headers use `constexpr char[]` rather than the `makeStr`/StaticStr
# DSL the span headers use, because the OTel C++ API takes nostd::string_view,
# which constructs from `char const*` but NOT from std::string_view.
METRIC_CONST_DEF = re.compile(
    r"inline\s+constexpr\s+char\s+(\w+)\s*\[\s*\]\s*=\s*\"([^\"]*)\"\s*;"
)


def find_metricname_headers(root: Path) -> List[Path]:
    """Every `*MetricNames.h` in the tree (the metric-side counterpart of
    `find_spanname_headers`)."""
    return sorted(
        p
        for p in list((root / "src").rglob("*MetricNames.h"))
        + list((root / "include").rglob("*MetricNames.h"))
        if p.is_file()
    )


def metric_constants(root: Path) -> Tuple[Set[str], Set[str], Set[str]]:
    """Return (instrument_names, label_keys, label_values) declared across the
    `*MetricNames.h` headers, split by which namespace block each constant sits
    in: `namespace metric` -> instrument names, `namespace label` -> label keys,
    `namespace lval` -> bounded label values.

    Comments are stripped first, so an illustrative `@code` example in a doc
    comment cannot seed the authoritative set (same reasoning as
    `strip_comments` for L1 spans).

    A constant in none of the three namespaces is ignored rather than guessed
    at, keeping the derivation conservative in the same direction as L1."""
    names: Set[str] = set()
    keys: Set[str] = set()
    values: Set[str] = set()
    for h in find_metricname_headers(root):
        text = strip_comments(read_source(h))
        for ns, bucket in (
            ("metric", names),
            ("label", keys),
            ("lval", values),
        ):
            for block in namespace_spans(text, ns):
                for m in METRIC_CONST_DEF.finditer(block):
                    bucket.add(m.group(2))
    return names, keys, values


def namespace_spans(text: str, want: str) -> List[str]:
    """Return the body text of each `namespace <want> { ... }` block, brace
    matched so nested namespaces (e.g. `lval::dns_resolve`) are contained in
    their parent's span. Generalizes `attr_namespace_spans`, which is hardcoded
    to `attr`."""
    spans: List[str] = []
    for opener in NS_OPEN.finditer(text):
        if opener.group(1).split("::")[-1] != want:
            continue
        i = opener.end()
        depth, start = 1, i
        while i < len(text) and depth > 0:
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            i += 1
        spans.append(text[start : i - 1])
    return spans


# Identity labels stamped by EXTERNAL infrastructure the OTel pipeline in this
# repo does not own: the perf-iac harness attaches these to every metric it
# scrapes so dashboards can filter by which build/role produced a series. They
# have no L1 (*SpanNames.h), L2 (collector config), or L6 (MetricsRegistry.cpp)
# source to derive from, so — unlike every other dashboard label — they cannot
# be validated dynamically. This is a deliberate, narrow exception to the "no
# hardcoded allowlist" design principle, kept separate from the generic
# Prometheus/Grafana `builtins` set below so it stays visible and auditable.
# Add a label here ONLY if it is genuinely injected by infra outside this
# repo's OTel code (never as a workaround for a dashboard querying a label
# that nothing actually emits — that is a real Rule D violation).
EXTERNAL_INFRA_LABELS = {
    "xrpl_work_item",  # perf-iac: ticket/work-item id for the perf comparison run
    "xrpl_branch",  # perf-iac: git ref of the xrpld build under test
    "xrpl_node_role",  # perf-iac: validator/peer role in the perf cluster
}


def run_rule_d_dashboards(
    root: Path, l1_keys: Set[str], metric_labels: Set[str], report: Report
) -> None:
    dash_dir = root / "docker" / "telemetry" / "grafana" / "dashboards"
    files = sorted(dash_dir.glob("*.json")) if dash_dir.is_dir() else []
    if not files:
        report.skip("D", "no dashboard JSON present")
        return
    if not l1_keys:
        report.skip("D", "no L1 key set to validate against")
        return
    builtins = {
        "__name__",  # Prometheus reserved label for the metric name itself
        "le",
        "exported_instance",
        "span_name",
        "status_code",
        "service_name",
        "service_version",
        "service_instance_id",
        "job",
        "job_type",  # standard Prometheus label for job-queue metrics
        "instance",
    }
    # A dashboard label is valid if it is a span attribute (L1), a native-metric
    # label (L6), a Prometheus/Grafana builtin, or an external-infra identity
    # label (EXTERNAL_INFRA_LABELS).
    valid = l1_keys | metric_labels | builtins | EXTERNAL_INFRA_LABELS
    found = False
    for f in files:
        try:
            text = read_source(f)
        except OSError:
            continue
        # PromQL `sum by (a, b)` and `{label="..."}` references.
        labels: Set[str] = set()
        for m in re.finditer(r"by\s*\(([^)]*)\)", text):
            labels |= {x.strip() for x in m.group(1).split(",") if x.strip()}
        for m in re.finditer(r"\b([a-z_][a-z0-9_.]*)\s*[=!]~?\s*\"", text):
            labels.add(m.group(1))
        for lbl in sorted(labels):
            # Strip a TraceQL scope prefix (span./resource./...) — the bare
            # attribute is what must resolve against L1.
            bare = TRACEQL_SCOPE.sub("", lbl)
            if bare in valid:
                continue
            found = True
            report.violation(
                "D",
                str(f.relative_to(root)),
                lbl,
                "must exist in L1, a metric label, or be a builtin",
            )
    if not found:
        report.ok(f"D: dashboard PromQL labels all resolve ({len(files)} file(s))")


def run_rule_e_runbook(root: Path, l1_keys: Set[str], report: Report) -> None:
    path = root / "docs" / "telemetry-runbook.md"
    if not path.is_file():
        report.skip("E", "runbook not present")
        return
    if not l1_keys:
        report.skip("E", "no L1 key set to validate against")
        return
    text = read_source(path)
    found = False
    # Only the dotted `xrpl.<domain>.<field>` attribute form is a violation. The
    # `xrpl.`-with-trailing-dot anchor is the discriminator: it matches the old
    # dotted attribute convention being migrated away from, while everything
    # else legitimately dotted in the runbook does NOT match it —
    #   * span names      (`consensus.round`, `tx.process`)   no `xrpl.` prefix
    #   * filenames       (`xrpld.cfg`, `RCLConsensus.cpp`)   `xrpld.`/`.cpp`, not `xrpl.`
    #   * OTel-standard   (`service.name`, `http.method`)     no `xrpl.` prefix
    #   * metric labels   (`xrpl_rpc_command`)                underscore, no dot
    # Legitimate dotted resource attrs (`xrpl.network.id`/`.type`) are in L1 and
    # are skipped. A dotted `xrpl.` token absent from L1 is a genuine doc/code
    # mismatch (e.g. `xrpl.tx.hash` where the code emits `tx_hash`).
    # EXTERNAL_INFRA_LABELS (Rule D) holds the underscore/metric-label form of
    # the perf-iac identity attrs; the resource-attribute layer stamps the same
    # identities dotted (xrpl.work.item/.branch/.node.role -- see the alloy
    # pipeline that owns them), so also skip a token whose dotted-to-underscore
    # form is in that set.
    external_infra_dotted = {lbl.replace("_", ".") for lbl in EXTERNAL_INFRA_LABELS}
    for m in re.finditer(r"`(xrpl\.[a-z][a-z0-9_.]*)`", text):
        token = m.group(1)
        if token in l1_keys:  # legitimate dotted resource attr (xrpl.network.*)
            continue
        if token in external_infra_dotted:  # perf-iac resource-attribute layer
            continue
        found = True
        report.violation(
            "E", str(path.relative_to(root)), token, "underscore, not dotted"
        )
    if not found:
        report.ok("E: runbook attribute references consistent with L1")


# ---------------------------------------------------------------------------
# Metric rules I / J / K
# ---------------------------------------------------------------------------

# A metric emit site whose name argument must be a constant. Two families:
#   * the XRPL_METRIC_* call-site macros, where arg0 is the app/registry and
#     arg1 is the instrument name;
#   * the OTel meter factories, where arg0 is the instrument name.
# Matching the macro by name (rather than by receiver, as CALLSITE does) is
# sufficient because the XRPL_METRIC_ prefix is unambiguous in this repo.
METRIC_MACRO_CALL = re.compile(r"\bXRPL_METRIC_([A-Z_]+)\s*\(")
METRIC_FACTORY_CALL = re.compile(
    r"\bCreate(?:UInt64|Int64|Double)"
    r"(?:Counter|UpDownCounter|Histogram|Gauge|ObservableGauge|"
    r"ObservableCounter|ObservableUpDownCounter)\s*\("
)
# A `{ KEY , ...` label pair opener inside an already-isolated argument list.
# KEY is what Rule I checks; the VALUE after the comma is exempt (runtime data).
METRIC_PAIR_KEY = re.compile(r"\{\s*(\"(?:[^\"\\]|\\.)*\"|[A-Za-z_][\w:]*)\s*,")

# The unit/kind suffix convention every instrument name must satisfy (Rule J).
# A cumulative counter reads correctly under rate() only if a reader can tell it
# from a gauge, and a duration is ambiguous unless it carries its unit -- the
# OTel `unit` argument is not surfaced on the Prometheus metric name.
METRIC_COUNTER_SUFFIX = "_total"
METRIC_DURATION_SUFFIXES = ("_us", "_ms", "_seconds")


def metric_calls(text: str):
    """Yield (kind, name_index, arglist, lineno) for every metric emit site.

    `kind` is the macro suffix (e.g. `COUNTER_INC_LABELED`) for a macro call, or
    the factory method name for a `meter->Create*` call. `name_index` is which
    top-level argument holds the instrument name."""
    for m in METRIC_MACRO_CALL.finditer(text):
        arglist = balanced_arglist(text, m.end())
        yield m.group(1), 1, arglist, text.count("\n", 0, m.start()) + 1
    for m in METRIC_FACTORY_CALL.finditer(text):
        arglist = balanced_arglist(text, m.end())
        name = m.group(0).rstrip("( \t\n")
        yield name, 0, arglist, text.count("\n", 0, m.start()) + 1


def balanced_arglist(text: str, start: int) -> str:
    """Return the argument-list text of a call whose '(' ends at `start`-1,
    balancing nesting and ignoring parens inside string literals."""
    i, depth, in_str, esc = start, 1, False, False
    while i < len(text) and depth > 0:
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return text[start : i - 1]


def run_rule_i_metric_literals(root: Path, report: Report) -> None:
    """Rule I: no string literal as a metric instrument NAME or label KEY.

    The metric-side mirror of Rule F. An instrument name or label key passed to
    an `XRPL_METRIC_*` macro or to a `meter->Create*` factory must reference a
    `*MetricNames.h` constant, so a rename is one edit instead of six and a typo
    is a compile error instead of a metric that silently never appears.

    Exempt, for the same reasons Rule F exempts them:
      * label VALUES -- the argument after a label key is runtime data;
      * the instrument DESCRIPTION -- prose, not naming surface;
      * `*MetricNames.h` itself -- that is where the strings are defined;
      * `MetricMacros.h` -- the macro definitions, whose `(name)` is a parameter;
      * test code -- tests pass arbitrary literals to exercise the API, and
        asserting on a literal is what proves a constant's value is unchanged.

    Scope -- a RATCHET, not a flag day. The rule fires only on a metric whose
    FAMILY already has constants declared in a `*MetricNames.h` (matched on the
    name's first underscore segment, e.g. `sync_`, `jobq_`, `unl_`). The metric
    surface predates this rule by many phases, and failing the build on ~27
    pre-existing instruments at once would force one un-reviewable mega-change.
    Declaring a constant for a family is what opts that family in, so:
      * a NEW metric in an already-converted family is caught immediately;
      * a literal left behind in a family being converted is caught;
      * an untouched legacy family stays green until someone converts it, at
        which point the whole family is enforced.
    A literal whose family has no constants at all is reported as a Rule L
    warning instead (non-fatal), so the remaining work stays visible.

    Presence-gated on `*MetricNames.h` existing at all; with no metric header
    in the tree the rule has no families to enforce and is skipped."""
    if not find_metricname_headers(root):
        report.skip("I", "no *MetricNames.h present")
        return
    names, keys, _ = metric_constants(root)
    owned = metric_prefixes(names)
    found = False
    deferred: List[Tuple[str, str]] = []

    def report_literal(rel: object, lineno: int, token: str, wire: str) -> None:
        nonlocal found
        # A label key is enforced once ANY label-key constant exists, because
        # the label vocabulary is shared across families -- `outcome` means the
        # same thing everywhere, so there is no per-family ratchet for it.
        in_scope = (
            any(wire.startswith(p) for p in owned) if token.startswith("name") else True
        )
        if in_scope:
            found = True
            report.violation(
                "I", f"{rel}:{lineno}", token, "use a *MetricNames.h constant"
            )
        else:
            deferred.append((f"{rel}:{lineno}", token))

    for path in sorted(iter_sources(root)):
        if path.name.endswith("MetricNames.h") or path.name == "MetricMacros.h":
            continue
        if is_test_path(path):
            continue
        text = strip_comments(read_source(path))
        rel = path.relative_to(root)
        for kind, name_idx, arglist, lineno in metric_calls(text):
            args = split_top_level_args(arglist)
            if len(args) > name_idx:
                lit = STRING_LITERAL.search(args[name_idx])
                if lit:
                    report_literal(
                        rel, lineno, f'name "{lit.group(1)}" ({kind})', lit.group(1)
                    )
            # Label KEYS live in the label-set initializer, e.g.
            # `{{"outcome", v}, {"reason", w}}`. Scanned over the WHOLE argument
            # list rather than per split argument, because
            # `split_top_level_args` balances only parentheses -- a brace-
            # enclosed label set contains top-level commas and would be split
            # apart mid-pair, hiding every key from the pattern.
            #
            # Scanning the whole list is safe: METRIC_PAIR_KEY requires an
            # opening `{` before the key, which the instrument name and the
            # description never have. Only the key position is checked; the
            # VALUE after it is runtime data and exempt.
            if keys:
                for pm in METRIC_PAIR_KEY.finditer(arglist):
                    key = pm.group(1)
                    if not key.startswith('"'):
                        continue
                    report_literal(rel, lineno, f"label {key} ({kind})", key.strip('"'))
    for loc, token in deferred:
        report.warning("L", loc, token, "metric family not yet converted")
    if not found:
        report.ok("I: no string-literal names/label keys in converted metric families")


def iter_sources(root: Path) -> List[Path]:
    """Every C++ source/header under src/ and include/."""
    return [
        p
        for base in ("src", "include")
        for ext in ("*.h", "*.cpp")
        for p in (root / base).rglob(ext)
        if p.is_file()
    ]


def instrument_kinds(root: Path, wire_by_symbol: Dict[str, str]) -> Dict[str, str]:
    """Map each declared instrument's WIRE name to its OTel instrument kind, by
    looking at how the emit sites actually create it.

    The kind is what decides which suffix is correct, so it must be read from
    the emit site rather than guessed from the name -- guessing from words like
    "latency" mislabels a multi-series gauge whose units live in its label
    VALUES (e.g. `nodestore_latency` observing `write_mean_us`), which is a
    legitimate shape, not a violation.

    Returns one of `counter`, `histogram`, `gauge`, `updown` per wire name.
    A name whose emit site is not found is absent from the result, so Rule J
    checks only the shape-independent rules for it."""
    kinds: Dict[str, str] = {}
    for path in iter_sources(root):
        if path.name == "MetricMacros.h" or path.name.endswith("MetricNames.h"):
            continue
        if is_test_path(path):
            continue
        text = strip_comments(read_source(path))
        for kind, name_idx, arglist, _ in metric_calls(text):
            args = split_top_level_args(arglist)
            if len(args) <= name_idx:
                continue
            arg = args[name_idx].strip()
            lit = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', arg)
            if lit:
                wire: Optional[str] = lit.group(1)
            else:
                ident = IDENTIFIER_ARG.match(arg)
                wire = (
                    wire_by_symbol.get(ident.group(1).split("::")[-1])
                    if ident
                    else None
                )
            if wire is None:
                continue
            kinds[wire] = classify_instrument_kind(kind)
    return kinds


def classify_instrument_kind(kind: str) -> str:
    """Normalise a macro suffix (`COUNTER_INC_LABELED`) or a factory method
    name (`CreateInt64ObservableGauge`) to one of counter/histogram/gauge/
    updown. Order matters: `UpDown` and `Observable` are checked before the
    bare `Counter` substring they both contain."""
    if "HISTOGRAM" in kind or "Histogram" in kind:
        return "histogram"
    if "UPDOWN" in kind or "UpDown" in kind:
        return "updown"
    if "GAUGE" in kind or "Gauge" in kind:
        return "gauge"
    if "COUNTER" in kind or "Counter" in kind:
        return "counter"
    return "other"


def symbol_wire_names(root: Path) -> Dict[str, str]:
    """Map each `*MetricNames.h` constant's C++ symbol name to its wire string,
    so an emit site referencing `metric::dnsResolveTotal` can be resolved back
    to `dns_resolve_total`."""
    out: Dict[str, str] = {}
    for h in find_metricname_headers(root):
        for m in METRIC_CONST_DEF.finditer(strip_comments(read_source(h))):
            out[m.group(1)] = m.group(2)
    return out


def run_rule_j_metric_suffixes(root: Path, report: Report) -> None:
    """Rule J: instrument names follow the naming and unit/kind suffix rules.

    Checked against the `namespace metric` constants in `*MetricNames.h`, which
    Rule I makes the only place an instrument name can be spelled. Enforced:

      * lower_snake_case (same shape as Rule G for span attributes);
      * no `xrpld_`/`xrpl_` prefix -- the Prometheus exporter adds the namespace
        itself, so a name carrying it would emit `xrpld_xrpld_*` on the wire;
      * a monotonic COUNTER ends in `_total`, so `rate()` over it reads
        correctly and a reader can tell it from a gauge;
      * a HISTOGRAM ends in `_us`, `_ms` or `_seconds`: histograms in this repo
        are all durations, and the OTel `unit` argument is not surfaced on the
        Prometheus metric name, so an unlabelled duration is ambiguous;
      * a GAUGE does not end in `_total`, which is reserved for counters.

    The instrument KIND comes from the emit site (see `instrument_kinds`), not
    from words in the name, so a multi-series gauge that carries its units in
    its label values is not mistaken for a mis-suffixed duration.

    Presence-gated: skipped when no `*MetricNames.h` exists."""
    if not find_metricname_headers(root):
        report.skip("J", "no *MetricNames.h present")
        return
    names, _, _ = metric_constants(root)
    if not names:
        report.skip("J", "no metric instrument-name constants declared")
        return
    kinds = instrument_kinds(root, symbol_wire_names(root))
    found = False

    def flag(name: str, expected: str) -> None:
        nonlocal found
        found = True
        report.violation("J", "*MetricNames.h", name, expected)

    for name in sorted(names):
        if not SNAKE_SEGMENT.match(name):
            flag(name, "must be lower_snake_case")
            continue
        if name.startswith(("xrpld_", "xrpl_")):
            flag(name, "drop the prefix; the exporter adds it")
            continue
        kind = kinds.get(name)
        if kind == "counter" and not name.endswith(METRIC_COUNTER_SUFFIX):
            flag(name, "counter must end in _total")
        elif kind == "histogram" and not name.endswith(METRIC_DURATION_SUFFIXES):
            flag(name, "histogram needs _us/_ms/_seconds suffix")
        elif kind == "gauge" and name.endswith(METRIC_COUNTER_SUFFIX):
            flag(name, "_total is reserved for counters")
    if not found:
        report.ok(f"J: {len(names)} metric instrument name(s) follow the naming rules")


def run_rule_k_expected_metrics(root: Path, report: Report) -> None:
    """Rule K: every metric named in the workload's `expected_metrics.json`
    exists as a `namespace metric` constant.

    The layer that cannot reference a C++ constant, so it is validated against
    one instead. This is the check that catches the failure mode this rule set
    exists for: a metric renamed in code while the workload validator still
    asserts on the old name, which fails as "metric never appeared" at runtime
    rather than at review time.

    Scope. Only groups whose metrics come from the OTel instrument API are
    checked. The file also inventories `beast::insight` statsd gauges/counters
    (`statsd_gauges`, `statsd_counters`) and collector-derived spanmetrics, whose
    names are minted by a different emit path -- `formatName()` lowercasing an
    insight metric, or the spanmetrics connector -- and so have no
    `*MetricNames.h` constant to resolve to by design. Checking them would fail
    the build on metrics this convention does not govern. Within the OTel groups
    the family ratchet still applies, so an unconverted family is out of scope
    too. Presence-gated on both layers."""
    path = root / "docker" / "telemetry" / "workload" / "expected_metrics.json"
    if not path.is_file():
        report.skip("K", "expected_metrics.json not present")
        return
    if not find_metricname_headers(root):
        report.skip("K", "no *MetricNames.h to validate against")
        return
    names, _, _ = metric_constants(root)
    try:
        doc = json.loads(read_source(path))
    except json.JSONDecodeError as exc:
        report.violation("K", str(path.relative_to(root)), str(exc), "valid JSON")
        return
    declared = {
        base_metric_name(e) for e in expected_metric_names(doc, NON_OTEL_METRIC_GROUPS)
    }
    # Only entries inside a metric FAMILY the constants already own are checked.
    # Anything else in the file predates *MetricNames.h, so demanding a constant
    # for it would fail the build on unrelated pre-existing metrics rather than
    # on the drift this rule targets.
    owned = metric_prefixes(names)
    unknown = sorted(
        n for n in declared if n not in names and any(n.startswith(p) for p in owned)
    )
    for n in unknown:
        report.violation(
            "K",
            str(path.relative_to(root)),
            n,
            "no matching *MetricNames.h constant",
        )
    if not unknown:
        checked = sum(1 for n in declared if n in names)
        report.ok(f"K: {checked} expected-metric name(s) resolve to constants")


# Suffixes the Prometheus exporter appends to a histogram instrument. The
# instrument name declared in code is the stem, so they are stripped before the
# constant lookup.
HISTOGRAM_SUFFIXES = ("_bucket", "_count", "_sum")


def base_metric_name(entry: str) -> str:
    """Reduce an `expected_metrics.json` entry to the bare instrument name.

    Entries are written the way an operator would query them, not the way the
    code declares them, so two forms have to be normalized away:
      * a PromQL label selector -- `sync_state{metric="ledgers_behind"}`;
      * an exporter-appended histogram suffix -- `..._ms_bucket`/`_count`/`_sum`,
        which the SDK adds and the code never names.
    Without this the rule would reject every entry in the file, which is a
    checker bug rather than a naming violation."""
    name = entry.split("{", 1)[0].strip()
    for suffix in HISTOGRAM_SUFFIXES:
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def metric_prefixes(names: Set[str]) -> Set[str]:
    """The first underscore-delimited segment of each declared instrument name,
    e.g. {`sync_`, `jobq_`, `unl_`}. Used by Rule K to decide whether an entry
    in `expected_metrics.json` belongs to a family this repo's constants own --
    so the rule flags a stale name inside a converted family without demanding a
    constant for every unrelated metric in the file."""
    return {n.split("_", 1)[0] + "_" for n in names if "_" in n}


# Groups in expected_metrics.json whose names are NOT minted by the OTel
# instrument API, and therefore have no *MetricNames.h constant by design:
#   statsd_gauges / statsd_counters -- beast::insight metrics, whose wire names
#     come from formatName() lowercasing an insight metric path;
#   spanmetrics -- synthesised by the collector's spanmetrics connector from
#     span names, not declared in C++ at all.
NON_OTEL_METRIC_GROUPS = frozenset({"statsd_gauges", "statsd_counters", "spanmetrics"})


def expected_metric_names(
    doc: object, skip_groups: frozenset = frozenset()
) -> Set[str]:
    """Collect metric names from `expected_metrics.json`.

    The file groups metrics by source (`spanmetrics`, `statsd_gauges`,
    `phase9_nodestore`, ...), each group holding a `metrics` LIST OF STRINGS
    alongside a prose `description`. Both that shape and a
    `[{"metric": "..."}]` shape are accepted, and the walk is generic, so a
    layout change degrades to "checks fewer names" rather than silently
    checking none. `description` is skipped explicitly -- it is prose, and
    letting it through would feed whole sentences to the family match.

    `skip_groups` drops whole top-level groups (see NON_OTEL_METRIC_GROUPS)."""
    out: Set[str] = set()

    def walk(node: object, in_metrics: bool = False) -> None:
        if isinstance(node, dict):
            for key, val in node.items():
                if key == "description" or key in skip_groups:
                    continue
                if key in ("metric", "name") and isinstance(val, str):
                    out.add(val)
                else:
                    walk(val, in_metrics or key == "metrics")
        elif isinstance(node, list):
            for item in node:
                if in_metrics and isinstance(item, str):
                    out.add(item)
                else:
                    walk(item, in_metrics)

    walk(doc)
    return out


if __name__ == "__main__":
    main()
