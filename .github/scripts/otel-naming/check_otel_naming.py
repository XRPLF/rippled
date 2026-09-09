#!/usr/bin/env python3

"""
Usage: check_otel_naming.py
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
  L4 dashboards: docker/telemetry/grafana/dashboards/*.json     (PromQL labels;
                 Loki/LogQL queries exempt -- see LOG_QUERY_DATASOURCES)
  L5 docs      : every *.md under docs/ and docker/telemetry/   (attr tables;
                 the set is DISCOVERED, never enumerated -- see
                 RULE_E_DOC_ROOTS)
  L6 metrics   : MetricsRegistry.cpp instrument labels          (native-metric
                 label keys, a valid dashboard-label source besides L1)

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
     prefixes are stripped before the L1 lookup. Log-datasource (Loki/LogQL)
     queries are exempt: their labels are minted by collector `regex_parser`
     captures or an in-query `| regexp` stage, so they have no L1/L6 source to
     resolve against (see LOG_QUERY_DATASOURCES). The exemption is per QUERY,
     not per file, so a mixed dashboard still has its PromQL panels checked.
  E  No dotted `xrpl.<domain>.<field>` attribute key in any L5 doc. The doc set
     is DISCOVERED, not listed: every `*.md` under RULE_E_DOC_ROOTS, so a doc
     added or renamed inside a root is covered without editing this script.
     Only the L1 resource
     attrs xrpl.network.* and the EXTERNAL_INFRA_LABELS dotted form --
     xrpl.work.item/.branch/.node.role -- may be dotted. Span names, filenames,
     OTel-standard keys, and metric labels are not flagged. A doc that names the
     wrong form as a deliberate counter-example opts that mention out with a
     key-scoped marker (RULE_E_MARKER_SYNTAX), which exempts ONLY the keys it
     lists on the line it appears on -- any other dotted token on that same line
     still fails.

Warnings (printed, but do NOT fail the build)
----------------------------------------------
  H  A constant referenced at a telemetry call-site is not defined in any
     *SpanNames.h. Span constants should live in the corresponding
     *SpanNames.h (single source of truth); defining one in-place bypasses the
     naming rules. A warning (not a failure) because the argument may instead
     be a legitimately dynamic local (e.g. a computed span-name leaf).
  E  A RULE_E_REQUIRED_DOCS anchor is not present in the tree (renamed, moved,
     or deleted -- so doc discovery can no longer be trusted to have found the
     docs that matter), an allow-dotted marker names no key (so it exempts
     nothing), or it names a key the line no longer mentions (a stale
     exemption). Warnings, not failures, because presence-gating must keep
     working on partial branches -- but the skip is now visible instead of
     silent.

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
# An OTel metric label map as emitted in C++: the `{{...}}` argument of an
# instrument call, e.g. `counter->Add(1, {{"job_type", a}, {"handler", b}})`.
# Matching the whole map first, rather than scanning the file for `{"key",`,
# keeps ordinary brace initializers (`{"http", "https", ...}`) out of the
# label set — they are not labels and must not license a dashboard filter.
METRIC_LABEL_MAP = re.compile(r"\{\{(.*?)\}\}", re.DOTALL)
# One key inside such a map: a string literal, or a `kFooLabel` constant name
# resolved through LABEL_CONST_DEF. Inside the map, each pair opens with `{`,
# except the first, whose `{` was consumed by the map's own `{{`.
METRIC_LABEL = re.compile(r'(?:^|\{)\s*"([a-z_][a-z0-9_]*)"\s*,')
METRIC_LABEL_CONST = re.compile(r"(?:^|\{)\s*(k[A-Za-z0-9_]*)\s*,")
# A label-key constant definition, with or without `inline`:
# `constexpr char kHandlerLabel[] = "handler";`
LABEL_CONST_DEF = re.compile(
    r'(?:inline\s+)?constexpr\s+char\s+(k[A-Za-z0-9_]*)\s*\[\s*\]\s*=\s*"([a-z_][a-z0-9_]*)"'
)


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
    run_rule_e_docs(root, l1_keys, report)

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

    A label key may be written inline as a string literal or hoisted into a
    named constant (`constexpr char kHandlerLabel[] = "handler";`, then
    `{{kHandlerLabel, value}}`). Both forms are collected, and the constants
    may be declared in a header, so headers are scanned as well as sources."""
    labels: Set[str] = set()
    # Constant name -> label string, gathered across every scanned file so a
    # `{{kFooLabel, ...}}` call site resolves even when the definition lives in
    # a different file from the instrument call.
    constants: Dict[str, str] = {}
    used_constants: Set[str] = set()
    for base in ("src", "include"):
        for pattern in ("*.cpp", "*.h"):
            for p in (root / base).rglob(pattern):
                if not p.is_file():
                    continue
                # Test code passes arbitrary literal pairs to exercise APIs
                # (`signers("alice", 1, {{"alice", 1}, {"bob", 2}})`). Those are
                # not metric labels, and must not license a dashboard filter.
                if is_test_path(p):
                    continue
                text = read_source(p)
                if "MetricsRegistry" not in p.name and "metric" not in text.lower():
                    continue
                constants.update(dict(LABEL_CONST_DEF.findall(text)))
                for label_map in METRIC_LABEL_MAP.findall(text):
                    labels |= set(METRIC_LABEL.findall(label_map))
                    used_constants |= set(METRIC_LABEL_CONST.findall(label_map))
    labels |= {constants[name] for name in used_constants if name in constants}
    return labels


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


# L5 doc layer (Rule E): the doc trees whose markdown publishes span-attribute
# keys a reader is expected to copy into a TraceQL/PromQL query. A dotted
# `xrpl.*` key left in any of them hands out a query that silently matches
# nothing, so the whole tree is checked, not just the operator runbook:
#   * docs/                  operator runbook, telemetry glossary, and the
#                            build/telemetry guides (recursively)
#   * docker/telemetry/       stack bring-up guide; its TraceQL/PromQL examples
#                            are meant to be pasted verbatim
#
# The set is DISCOVERED (every `*.md` under these roots), never enumerated. Two
# reasons this beats a hardcoded path list:
#   * it cannot go stale -- a telemetry doc added, renamed, or moved within a
#     root is picked up with no edit here, which is the same "derive it, do not
#     hardcode it" principle the rest of this script follows (design
#     principle 1), and coverage therefore grows with the docs; and
#   * it names only doc ROOTS that ship on the default branch, so the rule can
#     never be wired to a path that does not exist there.
# Each root is presence-gated (design principle 2): a branch that carries only
# one of them still gets that one checked. Both roots are also CI path-triggers
# for this check -- see `.github/workflows/on-pr.yml`.
RULE_E_DOC_ROOTS = (
    Path("docs"),
    Path("docker") / "telemetry",
)

# The anchor doc(s) discovery is REQUIRED to find. Discovery on its own can pass
# vacuously: if a root were renamed away, or emptied of markdown, Rule E would
# report a clean zero/near-zero-file run and nobody would notice the layer had
# stopped being checked. Demanding the one doc that is the whole reason Rule E
# exists turns that silent green into a visible warning (plus a count on the OK
# line). Deliberately tiny: this is a tripwire, not a second doc list. Every
# entry must live under RULE_E_DOC_ROOTS, or discovery could never find it.
RULE_E_REQUIRED_DOCS = (Path("docs") / "telemetry-runbook.md",)


def rule_e_docs(root: Path) -> List[Path]:
    """Discover the Rule E doc set: every `*.md` under RULE_E_DOC_ROOTS.

    Returns repo-relative paths, sorted for stable reporting and de-duplicated
    so overlapping roots (say `docs` and `docs/build`) cannot check one file
    twice. A root absent from the tree is skipped rather than treated as an
    error, so presence gating applies to the roots as it does to every other
    layer."""
    found: Set[Path] = set()
    for rel_root in RULE_E_DOC_ROOTS:
        base = root / rel_root
        if not base.is_dir():
            continue
        found.update(p.relative_to(root) for p in base.rglob("*.md") if p.is_file())
    return sorted(found)


# Line-scoped AND key-scoped opt-out for Rule E. Put it on a line that names a
# dotted key as a deliberate counter-example ("`tx_hash`, not `xrpl.tx.hash`") —
# a mention, not a published attribute key — and list exactly the keys that line
# is allowed to mention:
#
#   ... use `tx_hash`, not `xrpl.tx.hash`.
#   <!-- otel-naming:allow-dotted: xrpl.tx.hash -->
#
# The keys are part of the marker so an exemption cannot widen silently: a dotted
# token added to an already-marked line later, and not named in the marker, still
# fails. A marker with an empty key list therefore exempts NOTHING (and warns) —
# it is not a blanket line opt-out. NEVER use the marker to keep a real attribute
# table dotted; fix the table instead.
RULE_E_MARKER_SYNTAX = "<!-- otel-naming:allow-dotted: <key>[, <key>...] -->"
# The marker itself; group(1) is the raw key list (empty for the bare form).
# `[^>]` keeps the match inside one comment, so a marker cannot swallow the rest
# of the line, and the whole thing is matched per line (never across lines).
RULE_E_MARKER = re.compile(r"<!--\s*otel-naming:allow-dotted\s*:?\s*([^>]*?)\s*-->")
# Keys inside the marker are separated by commas and/or whitespace.
RULE_E_MARKER_KEY_SEP = re.compile(r"[,\s]+")
# The only doc form Rule E flags: a backticked dotted `xrpl.<domain>.<field>`.
RULE_E_DOTTED_TOKEN = re.compile(r"`(xrpl\.[a-z][a-z0-9_.]*)`")


def rule_e_allowed_keys(line: str) -> Tuple[Set[str], int]:
    """Parse every Rule-E allow-dotted marker on one line.

    Returns `(keys named by the markers, number of markers seen)`. The count is
    returned separately so the caller can tell "no marker" (enforce everything,
    silently) from "a marker that names no key" (enforce everything, and warn
    that the marker does nothing).

    Keys may be written bare or backticked and separated by commas and/or
    spaces, so both of these parse to the same two keys::

        <!-- otel-naming:allow-dotted: xrpl.tx.hash, xrpl.peer.id -->
        <!-- otel-naming:allow-dotted: `xrpl.tx.hash` `xrpl.peer.id` -->
    """
    keys: Set[str] = set()
    count = 0
    for m in RULE_E_MARKER.finditer(line):
        count += 1
        for raw in RULE_E_MARKER_KEY_SEP.split(m.group(1)):
            token = raw.strip().strip("`")
            if token:
                keys.add(token)
    return keys, count


# Datasource types whose query language draws its label names from the log
# stream at query time rather than from anything this repo's OTel code emits.
#
# A LogQL label has a third provenance Rule D cannot model: it is minted either
# by the collector's `regex_parser` named capture groups (partition, severity —
# see otel-collector-config.yaml) or by a `| regexp` stage inside the query
# itself (action, pk, state, mode, phase, jobname, ip, pubkey). Both create the
# label at ingest/query time, so no L1 (*SpanNames.h) or L6 (MetricsRegistry)
# lookup can ever resolve them — checking them against those layers reports a
# violation for a label that is correct by construction.
#
# This skips the QUERY, not the file: a dashboard mixing Prometheus and Loki
# panels still has its Prometheus panels validated.
LOG_QUERY_DATASOURCES = {"loki"}

# JSON keys that hold a query string (`expr` on panel targets, `query` on
# template variables).
QUERY_KEYS = ("expr", "query")


def datasource_type(node: object) -> Optional[str]:
    """Return the lowercased `datasource.type` of a dashboard node, if any.

    Grafana writes the datasource either as an object (`{"type": "loki",
    "uid": "..."}`) or, on template variables, as a bare uid string. Only the
    object form carries the type, which is what identifies the query language.
    """
    if isinstance(node, dict):
        ds = node.get("datasource")
        if isinstance(ds, dict):
            ds_type = ds.get("type")
            if isinstance(ds_type, str):
                return ds_type.lower()
    return None


def iter_dashboard_queries(node: object, inherited: Optional[str] = None):
    """Yield `(query_string, datasource_type)` for every query in a dashboard.

    `datasource_type` is the query's own datasource when it declares one, else
    the nearest enclosing one (a panel's datasource covers its targets), else
    None. Walks the whole tree so panels nested inside collapsed rows are not
    missed.
    """
    if isinstance(node, dict):
        current = datasource_type(node) or inherited
        for key in QUERY_KEYS:
            value = node.get(key)
            if isinstance(value, str) and value:
                yield value, current
        for value in node.values():
            yield from iter_dashboard_queries(value, current)
    elif isinstance(node, list):
        for value in node:
            yield from iter_dashboard_queries(value, inherited)


def promoted_resource_labels(root: Path) -> Set[str]:
    """Return resource attributes the collector promotes to metric labels.

    The spanmetrics connector's `resource_metrics_key_attributes` lists the
    resource attributes copied onto every derived metric datapoint, where the
    prometheus exporter rewrites dots to underscores. So `deployment.environment`
    in the collector config becomes the `deployment_environment` label a
    dashboard filters on. Both forms are returned: the underscore form for
    PromQL, the dotted form for TraceQL `resource.<key>` references.

    Derived from the config rather than hardcoded, so adding a key there is
    picked up automatically -- the same no-allowlist principle as Rules B and C.
    """
    path = root / "docker" / "telemetry" / "otel-collector-config.yaml"
    if not path.is_file():
        return set()
    try:
        text = read_source(path)
    except OSError:
        return set()
    labels: Set[str] = set()
    in_block = False
    for line in text.splitlines():
        if re.search(r"\bresource_metrics_key_attributes\s*:", line):
            in_block = True
            continue
        if in_block:
            m = re.match(r"\s*-\s*([A-Za-z0-9_.]+)\s*$", line)
            if m:
                key = m.group(1)
                labels.add(key.replace(".", "_"))
                labels.add(key)
            elif line.strip() and not line.lstrip().startswith("-"):
                in_block = False
    return labels


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
        # TraceQL intrinsics: fields of the span itself rather than attributes,
        # so they have no *SpanNames.h entry. `name` is the span name, matched
        # as `{name="consensus.accept"}`.
        "name",
        "duration",
        "kind",
        # Dotted form of the service identity, as TraceQL writes it
        # (`resource.service.instance.id` -> stripped to `service.instance.id`).
        "service.instance.id",
        "service.name",
        "service.version",
    }
    # A dashboard label is valid if it is a span attribute (L1), a native-metric
    # label (L6), a resource attribute the collector promotes onto metrics (L2),
    # a Prometheus/Grafana/TraceQL builtin, or an external-infra identity label
    # (EXTERNAL_INFRA_LABELS).
    valid = (
        l1_keys
        | metric_labels
        | promoted_resource_labels(root)
        | builtins
        | EXTERNAL_INFRA_LABELS
    )
    found = False
    checked = 0
    skipped_log_queries = 0
    for f in files:
        try:
            text = read_source(f)
        except OSError:
            continue
        # Parse so each query can be attributed to its datasource. Anything that
        # is not a well-formed dashboard object falls back to scanning the raw
        # text, which cannot attribute a datasource and so checks every query.
        # JSON validity itself is already enforced by the prettier pre-commit
        # hook, so this is a fallback and not a silent pass.
        try:
            queries = list(iter_dashboard_queries(json.loads(text)))
        except ValueError:
            queries = [(text, None)]
        labels: Set[str] = set()
        for query, ds_type in queries:
            if ds_type in LOG_QUERY_DATASOURCES:
                skipped_log_queries += 1
                continue
            checked += 1
            # PromQL `sum by (a, b)` and `{label="..."}` references. The
            # raw-text fallback still carries JSON's backslash-escaped quotes,
            # so accept an optional backslash before the quote.
            for m in re.finditer(r"by\s*\(([^)]*)\)", query):
                labels |= {x.strip() for x in m.group(1).split(",") if x.strip()}
            for m in re.finditer(r"\b([a-z_][a-z0-9_.]*)\s*[=!]~?\s*\\?\"", query):
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
        note = f"D: dashboard PromQL labels all resolve ({len(files)} file(s), {checked} query(s)"
        if skipped_log_queries:
            note += f", {skipped_log_queries} log query(s) skipped"
        report.ok(note + ")")


def run_rule_e_docs(root: Path, l1_keys: Set[str], report: Report) -> None:
    discovered = rule_e_docs(root)
    if not discovered:
        roots = ", ".join(str(rel) for rel in RULE_E_DOC_ROOTS)
        report.skip("E", f"no doc-layer file present (no *.md under {roots})")
        return
    # Presence-gating (design principle 2) must stay VISIBLE. Discovery cannot go
    # stale, but it can go QUIET: a root renamed away, or stripped of its
    # telemetry docs, still yields a clean run over whatever markdown is left, so
    # the layer would stop being checked while the rule reported green. Warn per
    # missing anchor — non-fatal, so a partial branch still passes — and name the
    # count on the OK line, so the doc set cannot quietly shrink.
    missing_required = [
        rel for rel in RULE_E_REQUIRED_DOCS if not (root / rel).is_file()
    ]
    for rel in missing_required:
        report.warning(
            "E", str(rel), "absent", "required Rule E doc not in tree (renamed?)"
        )
    if not l1_keys:
        report.skip("E", "no L1 key set to validate against")
        return
    found = False
    # Only the dotted `xrpl.<domain>.<field>` attribute form is a violation. The
    # `xrpl.`-with-trailing-dot anchor is the discriminator: it matches the old
    # dotted attribute convention being migrated away from, while everything
    # else legitimately dotted in these docs does NOT match it —
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
    # A doc that TEACHES the convention has to be able to name the wrong form as
    # a counter-example ("`tx_hash`, not `xrpl.tx.hash`"). The allow-dotted
    # marker is the opt-out for exactly that: the token is a mention, not a
    # published attribute key. It follows the repo's existing inline-marker
    # precedent (`<!-- cspell:ignore ... -->`), and is scoped BOTH to the line
    # and to the keys it names, so it can neither exempt a whole table nor grow
    # to cover a violation appended to an already-marked line.
    external_infra_dotted = {lbl.replace("_", ".") for lbl in EXTERNAL_INFRA_LABELS}
    for rel in discovered:
        for lineno, line in enumerate(read_source(root / rel).splitlines(), start=1):
            allowed, markers = rule_e_allowed_keys(line)
            if markers and not allowed:
                report.warning(
                    "E",
                    f"{rel}:{lineno}",
                    "allow-dotted",
                    f"marker names no key: exempts nothing. Use {RULE_E_MARKER_SYNTAX}",
                )
            tokens = [m.group(1) for m in RULE_E_DOTTED_TOKEN.finditer(line)]
            for token in tokens:
                if token in l1_keys:  # legitimate dotted resource attr (xrpl.network.*)
                    continue
                if token in external_infra_dotted:  # perf-iac resource-attr layer
                    continue
                if token in allowed:  # named counter-example on this line
                    continue
                found = True
                report.violation(
                    "E",
                    f"{rel}:{lineno}",
                    token,
                    "underscore, not dotted",
                )
            # A key the marker names but the line no longer mentions is a stale
            # exemption: harmless today, but it is how a marker silently starts
            # covering more than the author reviewed. Flagged, never fatal.
            for stale in sorted(allowed.difference(tokens)):
                report.warning(
                    "E", f"{rel}:{lineno}", stale, "allow-dotted key not on this line"
                )
    if not found:
        note = (
            "E: doc attribute references consistent with L1 "
            f"({len(discovered)} file(s) checked"
        )
        if missing_required:
            note += (
                f", {len(missing_required)} of {len(RULE_E_REQUIRED_DOCS)} "
                "required doc(s) absent"
            )
        report.ok(note + ")")


if __name__ == "__main__":
    main()
