#!/usr/bin/env python3

"""Unit tests for check_otel_naming.py.

Stdlib-only (unittest), matching the dependency-free policy of the check itself.
Run from anywhere:

    python .github/scripts/otel-naming/test_check_otel_naming.py

Each rule is exercised in isolation against a synthetic tree / synthetic L1 key
set, covering positive (must flag), negative (must not flag), and boundary
cases. Rule E (runbook dotted-attribute detection) has the densest coverage
because its discriminator — the `xrpl.<domain>.` prefix vs span names,
filenames, OTel-standard keys, and metric labels — is the subtlest.
"""

import contextlib
import importlib.util
import io
import shutil
import tempfile
import unittest
from pathlib import Path

# Load the check module by path (it is not an importable package).
_spec = importlib.util.spec_from_file_location(
    "check_otel_naming", str(Path(__file__).with_name("check_otel_naming.py"))
)
chk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(chk)


# A controlled L1 set used across tests: the two legitimate dotted resource
# attrs plus a handful of underscore span-attribute keys.
L1 = {
    "xrpl.network.id",
    "xrpl.network.type",
    "tx_hash",
    "peer_id",
    "consensus_mode",
    "command",
    "rpc_status",
    "ledger_seq",
}


def _run_rule_e(runbook_text: str):
    """Run Rule E against a synthetic runbook; return the flagged tokens."""
    d = Path(tempfile.mkdtemp())
    try:
        (d / "docs").mkdir()
        (d / "docs" / "telemetry-runbook.md").write_text(runbook_text)
        report = chk.Report()
        chk.run_rule_e_runbook(d, set(L1), report)
        return sorted(v[2] for v in report.violations)
    finally:
        shutil.rmtree(d)


class RuleERunbook(unittest.TestCase):
    """Rule E: only dotted `xrpl.<domain>.<field>` attribute keys are flagged."""

    # ----- positive: genuine dotted attribute-key violations -----
    def test_single_dotted_attr(self):
        self.assertEqual(_run_rule_e("`xrpl.tx.hash`"), ["xrpl.tx.hash"])

    def test_multiple_dotted_attrs(self):
        self.assertEqual(
            _run_rule_e("`xrpl.tx.hash` and `xrpl.consensus.mode`"),
            ["xrpl.consensus.mode", "xrpl.tx.hash"],
        )

    def test_deep_dotted_three_segments(self):
        self.assertEqual(
            _run_rule_e("`xrpl.consensus.ledger.seq`"), ["xrpl.consensus.ledger.seq"]
        )

    def test_dotted_attr_with_underscore_field(self):
        self.assertEqual(
            _run_rule_e("`xrpl.consensus.round_id`"), ["xrpl.consensus.round_id"]
        )

    def test_repeated_token_reported_each_occurrence(self):
        self.assertEqual(
            _run_rule_e("`xrpl.tx.hash` ... `xrpl.tx.hash`"),
            ["xrpl.tx.hash", "xrpl.tx.hash"],
        )

    def test_resource_attr_not_in_l1_is_flagged(self):
        self.assertEqual(
            _run_rule_e("`xrpl.network.unknown`"), ["xrpl.network.unknown"]
        )

    # ----- negative: legitimately-dotted tokens that must NOT be flagged -----
    def test_span_name_single(self):
        self.assertEqual(_run_rule_e("`consensus.round`"), [])

    def test_span_name_multi_segment(self):
        self.assertEqual(
            _run_rule_e("`consensus.phase.open` `rpc.command.server_info`"), []
        )

    def test_filename_cfg(self):
        self.assertEqual(_run_rule_e("`xrpld.cfg`"), [])

    def test_filename_cpp(self):
        self.assertEqual(_run_rule_e("`RCLConsensus.cpp`"), [])

    def test_otel_standard_service_name(self):
        self.assertEqual(_run_rule_e("`service.name`"), [])

    def test_otel_standard_http_method(self):
        self.assertEqual(_run_rule_e("`http.method`"), [])

    def test_metric_label_underscore(self):
        self.assertEqual(_run_rule_e("`xrpl_rpc_command`"), [])

    def test_bare_underscore_attrs(self):
        self.assertEqual(_run_rule_e("`tx_hash` `consensus_mode`"), [])

    def test_legit_dotted_resource_attrs_in_l1(self):
        self.assertEqual(_run_rule_e("`xrpl.network.id` `xrpl.network.type`"), [])

    def test_prose_word(self):
        self.assertEqual(_run_rule_e("the `command` attribute"), [])

    def test_plain_prose_no_backticks(self):
        self.assertEqual(_run_rule_e("xrpl.tx.hash without backticks is prose"), [])

    # ----- boundary -----
    def test_empty_runbook(self):
        self.assertEqual(_run_rule_e(""), [])

    def test_lookalike_prefix_xrpld(self):
        # `xrpld.` is NOT `xrpl.` — must not match.
        self.assertEqual(_run_rule_e("`xrpld.foo`"), [])

    def test_lookalike_prefix_underscore(self):
        # `xrpl_rpc.command` starts with `xrpl_`, not `xrpl.`.
        self.assertEqual(_run_rule_e("`xrpl_rpc.command`"), [])

    def test_uppercase_segment_not_matched(self):
        # The pattern requires a lowercase char after `xrpl.`; uppercase keys are
        # caught by Rule G at the L1 layer, not by the runbook text scan.
        self.assertEqual(_run_rule_e("`xrpl.TX.hash`"), [])

    def test_token_touching_table_pipes(self):
        self.assertEqual(_run_rule_e("| `xrpl.tx.hash` | desc |"), ["xrpl.tx.hash"])

    def test_mixed_line_only_xrpl_dotted_flagged(self):
        self.assertEqual(
            _run_rule_e("`consensus.round` uses `xrpl.tx.hash` and `service.name`"),
            ["xrpl.tx.hash"],
        )

    def test_skips_when_runbook_absent(self):
        d = Path(tempfile.mkdtemp())
        try:
            report = chk.Report()
            chk.run_rule_e_runbook(d, set(L1), report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("SKIP: E" in s for s in report.skips))
        finally:
            shutil.rmtree(d)

    def test_skips_when_l1_empty(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "docs").mkdir()
            (d / "docs" / "telemetry-runbook.md").write_text("`xrpl.tx.hash`")
            report = chk.Report()
            chk.run_rule_e_runbook(d, set(), report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("SKIP: E" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


class DslParser(unittest.TestCase):
    """The makeStr/join/seg:: constexpr DSL resolver — the foundation of the
    L1 key set. Covers flat, nested, cross-file, alias, and multi-line forms."""

    def test_flat_join(self):
        syms = chk.resolve_constants(
            'inline constexpr auto a = makeStr("xrpl");\n'
            'inline constexpr auto b = makeStr("network");\n'
            "inline constexpr auto c = join(a, b);\n"
        )
        self.assertEqual(syms["c"], "xrpl.network")

    def test_nested_join_three_segments(self):
        syms = chk.resolve_constants(
            'inline constexpr auto xrpl = makeStr("xrpl");\n'
            'inline constexpr auto network = makeStr("network");\n'
            "inline constexpr auto networkId = "
            'join(join(xrpl, network), makeStr("id"));\n'
        )
        self.assertEqual(syms["networkId"], "xrpl.network.id")

    def test_qualified_seg_reference(self):
        # `seg::rpc` resolves by its bare leaf `rpc`.
        syms = chk.resolve_constants('inline constexpr auto rpc = makeStr("rpc");\n')
        syms2 = chk.resolve_constants(
            'inline constexpr auto command = join(seg::rpc, makeStr("command"));\n',
            syms,
        )
        self.assertEqual(syms2["command"], "rpc.command")

    def test_alias_reference(self):
        syms = chk.resolve_constants('inline constexpr auto rpc = makeStr("rpc");\n')
        chk.resolve_constants("inline constexpr auto alias = seg::rpc;\n", syms)
        self.assertEqual(syms["alias"], "rpc")

    def test_unresolvable_expr_omitted(self):
        syms = chk.resolve_constants("inline constexpr auto x = join(unknown, y);\n")
        self.assertNotIn("x", syms)

    def test_split_top_level_args_respects_nesting(self):
        self.assertEqual(
            chk.split_top_level_args("join(seg::a, b), c"),
            ["join(seg::a, b)", " c"],
        )

    def test_split_top_level_args_ignores_comma_in_string(self):
        self.assertEqual(
            chk.split_top_level_args('key, ","'),
            ["key", ' ","'],
        )

    def test_strip_comments_removes_line_and_block(self):
        self.assertEqual(
            chk.strip_comments("a // line\nb /* blk */ c").split(),
            ["a", "b", "c"],
        )


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def _header(ns_attr_body: str, prefix_seg: str = "") -> str:
    """A minimal *SpanNames.h body: optional seg defs + a namespace attr block."""
    return (
        "#pragma once\n"
        + prefix_seg
        + "namespace xrpl::telemetry::demo::span {\n"
        + "namespace attr {\n"
        + ns_attr_body
        + "}  // namespace attr\n"
        + "}\n"
    )


class AttrKeyExtraction(unittest.TestCase):
    """attr_keys_from_header: comment-stripping + multi-line + using re-export."""

    def _l1(self, header_text):
        d = Path(tempfile.mkdtemp())
        try:
            h = d / "src" / "DemoSpanNames.h"
            _write(h, header_text)
            syms = chk.build_global_symbols([h])
            return chk.attr_keys_from_header(h, syms)
        finally:
            shutil.rmtree(d)

    def test_single_line_makestr(self):
        keys = self._l1(_header('inline constexpr auto k = makeStr("tx_hash");\n'))
        self.assertIn("tx_hash", keys)

    def test_multiline_constexpr_captured(self):
        keys = self._l1(
            _header("inline constexpr auto k =\n" '    makeStr("round_time_ms");\n')
        )
        self.assertIn("round_time_ms", keys)

    def test_commented_makestr_not_leaked(self):
        keys = self._l1(
            _header(
                'inline constexpr auto k = makeStr("good");\n'
                '// inline constexpr auto bad = makeStr("old.dotted");\n'
            )
        )
        self.assertIn("good", keys)
        self.assertNotIn("old.dotted", keys)

    def test_block_commented_makestr_not_leaked(self):
        keys = self._l1(
            _header(
                'inline constexpr auto k = makeStr("good");\n'
                '/* makeStr("blockbad") */\n'
            )
        )
        self.assertNotIn("blockbad", keys)


class CamelToDotSegments(unittest.TestCase):
    """semconv CamelCase -> dotted OTel-standard key derivation."""

    def test_service_instance_id(self):
        self.assertEqual(
            chk.camel_to_dotsegments("ServiceInstanceId"),
            ["service", "instance", "id"],
        )

    def test_service_name(self):
        self.assertEqual(chk.camel_to_dotsegments("ServiceName"), ["service", "name"])

    def test_derive_keys_from_telemetry_cpp(self):
        d = Path(tempfile.mkdtemp())
        try:
            tele = d / "src" / "libxrpl" / "telemetry" / "Telemetry.cpp"
            _write(
                tele,
                "resource::Resource::Create({\n"
                "  {semconv::service::kServiceName, x},\n"
                "  {semconv::service::kServiceInstanceId, y},\n"
                "});\n",
            )
            report = chk.Report()
            allow = chk.derive_dotted_resource_keys(d, {}, report)
            self.assertIn("service.name", allow)
            self.assertIn("service.instance.id", allow)
        finally:
            shutil.rmtree(d)


class SymbolCollision(unittest.TestCase):
    """attr_keys_from_header must resolve a constant against ITS OWN header, so
    two headers defining a same-named constant each report their real wire key.
    Regression for the flat-symbol-table collision that let a later header
    clobber an earlier one and erased a dotted key from L1 (a Rule-A blind
    spot)."""

    def _build(self, files):
        d = Path(tempfile.mkdtemp())
        paths = {}
        for rel, text in files.items():
            p = d / rel
            _write(p, text)
            paths[rel] = p
        return d, paths

    def test_same_named_const_not_clobbered_across_headers(self):
        base = (
            "#pragma once\n"
            "namespace xrpl::telemetry {\n"
            'namespace seg { inline constexpr auto xrpl = makeStr("xrpl");\n'
            'inline constexpr auto ledger = makeStr("ledger"); }\n'
            "namespace attr {\n"
            "inline constexpr auto ledgerHash = "
            'join(join(seg::xrpl, seg::ledger), makeStr("hash"));\n'
            "}\n}\n"
        )
        cons = (
            "#pragma once\n"
            "namespace xrpl::telemetry::consensus::span {\n"
            "namespace attr { inline constexpr auto ledgerHash = "
            'makeStr("ledger_hash"); }\n}\n'
        )
        d, paths = self._build(
            {
                "include/xrpl/telemetry/SpanNames.h": base,
                "src/xrpld/consensus/ConsensusSpanNames.h": cons,
            }
        )
        try:
            headers = chk.find_spanname_headers(d)
            syms = chk.build_global_symbols(headers)
            by_name = {p.name: chk.attr_keys_from_header(p, syms) for p in headers}
            # The base header keeps its dotted key; consensus keeps the bare one.
            self.assertIn("xrpl.ledger.hash", by_name["SpanNames.h"])
            self.assertEqual(by_name["ConsensusSpanNames.h"], {"ledger_hash"})
        finally:
            shutil.rmtree(d)

    def test_using_reexport_still_resolves_globally(self):
        # A `using`-re-export imports a constant defined elsewhere; it must
        # resolve against the global table, not the local header.
        base = (
            "#pragma once\n"
            "namespace xrpl::telemetry {\n"
            "namespace attr { inline constexpr auto txHash = "
            'makeStr("tx_hash"); }\n}\n'
        )
        dom = (
            "#pragma once\n"
            "namespace xrpl::telemetry::tx::span {\n"
            "namespace attr { using ::xrpl::telemetry::attr::txHash; }\n}\n"
        )
        d, paths = self._build(
            {
                "include/xrpl/telemetry/SpanNames.h": base,
                "src/xrpld/app/misc/TxSpanNames.h": dom,
            }
        )
        try:
            headers = chk.find_spanname_headers(d)
            syms = chk.build_global_symbols(headers)
            keys = chk.attr_keys_from_header(
                paths["src/xrpld/app/misc/TxSpanNames.h"], syms
            )
            self.assertEqual(keys, {"tx_hash"})
        finally:
            shutil.rmtree(d)


class ResourceAllowlistScope(unittest.TestCase):
    """derive_dotted_resource_keys must allowlist ONLY the dotted keys actually
    passed to Resource::Create() — not every dotted key in the base header. A
    dotted attr declared in a header but not set as a resource attr is a Rule-A
    violation."""

    def _derive(self, tele_text, span_text):
        d = Path(tempfile.mkdtemp())
        try:
            _write(d / "src" / "libxrpl" / "telemetry" / "Telemetry.cpp", tele_text)
            _write(d / "include" / "xrpl" / "telemetry" / "SpanNames.h", span_text)
            headers = chk.find_spanname_headers(d)
            syms = chk.build_global_symbols(headers)
            allow = chk.derive_dotted_resource_keys(d, syms, chk.Report())
            return allow, syms, headers, d
        except Exception:
            shutil.rmtree(d)
            raise

    def test_dotted_span_attr_not_allowlisted_and_flagged(self):
        span = (
            "#pragma once\n"
            "namespace xrpl::telemetry {\n"
            'namespace seg { inline constexpr auto xrpl = makeStr("xrpl");\n'
            'inline constexpr auto ledger = makeStr("ledger");\n'
            'inline constexpr auto network = makeStr("network"); }\n'
            "namespace attr {\n"
            "inline constexpr auto networkId = "
            'join(join(seg::xrpl, seg::network), makeStr("id"));\n'
            "inline constexpr auto ledgerHash = "
            'join(join(seg::xrpl, seg::ledger), makeStr("hash"));\n'
            "}\n}\n"
        )
        tele = (
            "auto r = resource::Resource::Create({\n"
            "  {semconv::service::kServiceName, x},\n"
            "  {std::string(attr::networkId), n},\n"
            "});\n"
        )
        allow, syms, headers, d = self._derive(tele, span)
        try:
            # networkId IS a resource attr; ledgerHash is NOT, despite living in
            # the base header.
            self.assertIn("xrpl.network.id", allow)
            self.assertNotIn("xrpl.ledger.hash", allow)
            kbh = {h: chk.attr_keys_from_header(h, syms) for h in headers}
            report = chk.Report()
            chk.run_rule_a(kbh, allow, report)
            self.assertEqual([v[2] for v in report.violations], ["xrpl.ledger.hash"])
        finally:
            shutil.rmtree(d)

    def test_resource_block_brace_matched(self):
        # A nested {key,value} initializer must not truncate the block scan.
        tele = (
            "auto r = resource::Resource::Create({\n"
            "  {semconv::service::kServiceName, x},\n"
            "  {std::string(attr::networkType), t},\n"
            "});\n"
        )
        span = (
            "#pragma once\n"
            "namespace xrpl::telemetry {\n"
            'namespace seg { inline constexpr auto xrpl = makeStr("xrpl");\n'
            'inline constexpr auto network = makeStr("network"); }\n'
            "namespace attr { inline constexpr auto networkType = "
            'join(join(seg::xrpl, seg::network), makeStr("type")); }\n}\n'
        )
        allow, _syms, _headers, d = self._derive(tele, span)
        try:
            self.assertIn("xrpl.network.type", allow)
            self.assertIn("service.name", allow)
        finally:
            shutil.rmtree(d)


def _run_rule_a(keys_by_header, allow):
    report = chk.Report()
    chk.run_rule_a(keys_by_header, allow, report)
    return sorted(v[2] for v in report.violations)


class RuleADotted(unittest.TestCase):
    def test_dotted_attr_not_in_allow_flagged(self):
        kbh = {Path("src/RpcSpanNames.h"): {"xrpl.tx.hash", "command"}}
        self.assertEqual(_run_rule_a(kbh, {"xrpl.network.id"}), ["xrpl.tx.hash"])

    def test_resource_attr_in_allow_passes(self):
        kbh = {Path("src/SpanNames.h"): {"xrpl.network.id"}}
        self.assertEqual(_run_rule_a(kbh, {"xrpl.network.id"}), [])

    def test_bare_key_never_flagged(self):
        kbh = {Path("src/TxSpanNames.h"): {"tx_hash", "command"}}
        self.assertEqual(_run_rule_a(kbh, set()), [])


def _run_rule_g(keys_by_header):
    report = chk.Report()
    chk.run_rule_g(keys_by_header, report)
    return sorted(v[2] for v in report.violations)


class RuleGSnakeCase(unittest.TestCase):
    def test_camelcase_flagged(self):
        self.assertEqual(_run_rule_g({Path("h"): {"txHash"}}), ["txHash"])

    def test_uppercase_flagged(self):
        self.assertEqual(_run_rule_g({Path("h"): {"TX_HASH"}}), ["TX_HASH"])

    def test_space_flagged(self):
        self.assertEqual(_run_rule_g({Path("h"): {"bad key"}}), ["bad key"])

    def test_snake_case_passes(self):
        self.assertEqual(_run_rule_g({Path("h"): {"tx_hash", "rpc_status"}}), [])

    def test_dotted_resource_segments_pass(self):
        self.assertEqual(_run_rule_g({Path("h"): {"xrpl.network.id"}}), [])

    def test_dotted_with_bad_segment_flagged(self):
        self.assertEqual(
            _run_rule_g({Path("h"): {"xrpl.Network.id"}}), ["xrpl.Network.id"]
        )


class RuleFAndH(unittest.TestCase):
    """run_rule_f: literal keys/span-names flagged; values & tests exempt.
    Rule H: qualified constant not in any header warns (non-fatal)."""

    def _run(self, rel_path, source, header_symbols=frozenset()):
        d = Path(tempfile.mkdtemp())
        try:
            _write(d / rel_path, source)
            report = chk.Report()
            chk.run_rule_f(d, report, set(header_symbols))
            return (
                sorted(v[2] for v in report.violations),
                sorted(w[2] for w in report.warnings),
            )
        finally:
            shutil.rmtree(d)

    def test_literal_key_flagged(self):
        v, _ = self._run("src/Foo.cpp", 'g.setAttribute("lit_key", v);\n')
        self.assertEqual(v, ['setAttribute arg0 "lit_key"'])

    def test_literal_value_exempt(self):
        v, _ = self._run("src/Foo.cpp", 'g.setAttribute(attr::command, "submit");\n')
        self.assertEqual(v, [])

    def test_span_name_args_flagged(self):
        v, _ = self._run("src/Foo.cpp", 'SpanGuard::span(cat, "rpc", "command");\n')
        self.assertEqual(v, ['span arg1 "rpc"', 'span arg2 "command"'])

    def test_rootspan_literal_flagged_by_rule_f(self):
        # rootSpan(cat, prefix, name) shares span()'s signature, so a string
        # literal in the prefix/name position must FAIL rule F exactly as it
        # does for span() — otherwise a call switched to rootSpan silently
        # escapes span-name validation.
        v, _ = self._run(
            "src/Foo.cpp",
            'SpanGuard::rootSpan(cat, "peer", "validation.receive");\n',
        )
        self.assertEqual(
            v, ['rootSpan arg1 "peer"', 'rootSpan arg2 "validation.receive"']
        )

    def test_rootspan_constant_args_accepted(self):
        # Constant references in the prefix/name position are accepted (no
        # rule F), mirroring span()'s constant-arg handling.
        v, _ = self._run(
            "src/Foo.cpp",
            "SpanGuard::rootSpan(TraceCategory::Peer, seg::peer, "
            "peer_span::op::validationReceive);\n",
        )
        self.assertEqual(v, [])

    def test_test_path_exempt(self):
        v, _ = self._run("src/test/Foo.cpp", 'g.setAttribute("lit_key", v);\n')
        self.assertEqual(v, [])

    def test_spannames_header_exempt(self):
        v, _ = self._run("src/DemoSpanNames.h", 'g.setAttribute("lit_key", v);\n')
        self.assertEqual(v, [])

    def test_bare_span_call_not_matched(self):
        # No SpanGuard/./-> receiver -> not a telemetry call-site.
        v, _ = self._run("src/Foo.cpp", 'auto s = span("not", "telemetry");\n')
        self.assertEqual(v, [])

    def test_multiline_call_reports_first_line(self):
        v, _ = self._run("src/Foo.cpp", 'g.setAttribute(\n    "k",\n    v);\n')
        self.assertEqual(v, ['setAttribute arg0 "k"'])

    def test_paren_in_string_value_does_not_break_parsing(self):
        # The ")" inside the value must not end the call early; key still seen.
        v, _ = self._run("src/Foo.cpp", 'g.setAttribute("k", ")");\n')
        self.assertEqual(v, ['setAttribute arg0 "k"'])

    def test_rule_h_qualified_constant_warns(self):
        v, w = self._run(
            "src/Foo.cpp",
            "g.setAttribute(consensus::span::accept, v);\n",
            header_symbols={"command"},
        )
        self.assertEqual(v, [])
        self.assertEqual(w, ["setAttribute arg0 consensus::span::accept"])

    def test_rule_h_known_constant_no_warning(self):
        _, w = self._run(
            "src/Foo.cpp",
            "g.setAttribute(rpc_span::attr::command, v);\n",
            header_symbols={"command"},
        )
        self.assertEqual(w, [])

    def test_rule_h_bare_local_no_warning(self):
        _, w = self._run(
            "src/Foo.cpp", "g.setAttribute(myLeaf, v);\n", header_symbols={"command"}
        )
        self.assertEqual(w, [])


class RuleBCollector(unittest.TestCase):
    def _run(self, yaml_text, l1):
        d = Path(tempfile.mkdtemp())
        try:
            _write(d / "docker" / "telemetry" / "otel-collector-config.yaml", yaml_text)
            report = chk.Report()
            chk.run_rule_b_collector(d, set(l1), report)
            return sorted(v[2] for v in report.violations), report.skips
        finally:
            shutil.rmtree(d)

    def test_dimension_not_in_l1_flagged(self):
        y = "spanmetrics:\n  dimensions:\n    - name: bogus_dim\n    - name: command\n"
        v, _ = self._run(y, {"command"})
        self.assertEqual(v, ["bogus_dim"])

    def test_all_dimensions_in_l1_pass(self):
        y = "spanmetrics:\n  dimensions:\n    - name: command\n    - name: rpc_status\n"
        v, _ = self._run(y, {"command", "rpc_status"})
        self.assertEqual(v, [])

    def test_skip_when_no_spanmetrics_block(self):
        v, skips = self._run("receivers:\n  otlp:\n", {"command"})
        self.assertEqual(v, [])
        self.assertTrue(any("SKIP: B" in s for s in skips))


class RuleCTempo(unittest.TestCase):
    """Rule C reads the Grafana Tempo DATASOURCE file's search.filters and
    validates only span-scope tags against L1."""

    DS = "docker/telemetry/grafana/provisioning/datasources/tempo.yaml"

    def _run(self, yaml_text, l1):
        d = Path(tempfile.mkdtemp())
        try:
            _write(d / self.DS, yaml_text)
            report = chk.Report()
            chk.run_rule_c_tempo(d, set(l1), report)
            return sorted(v[2] for v in report.violations), report.skips
        finally:
            shutil.rmtree(d)

    def _filter(self, fid, tag, scope):
        return (
            f"          - id: {fid}\n"
            f"            tag: {tag}\n"
            f'            operator: "="\n'
            f"            scope: {scope}\n"
            f"            type: static\n"
        )

    def test_span_tag_not_in_l1_flagged(self):
        y = "search:\n        filters:\n" + self._filter("f1", "bogus_tag", "span")
        v, _ = self._run(y, {"command"})
        self.assertEqual(v, ["bogus_tag"])

    def test_span_tags_in_l1_pass(self):
        y = (
            "search:\n        filters:\n"
            + self._filter("f1", "command", "span")
            + self._filter("f2", "tx_hash", "span")
        )
        v, _ = self._run(y, {"command", "tx_hash"})
        self.assertEqual(v, [])

    def test_resource_and_intrinsic_tags_ignored(self):
        # service.* (resource) and name/status/duration (intrinsic) are not
        # span attributes — they must not be validated against L1.
        y = (
            "search:\n        filters:\n"
            + self._filter("f1", "service.instance.id", "resource")
            + self._filter("f2", "name", "intrinsic")
            + self._filter("f3", "duration", "intrinsic")
        )
        v, skips = self._run(y, {"command"})
        self.assertEqual(v, [])
        self.assertTrue(any("SKIP: C" in s for s in skips))

    def test_skip_when_datasource_absent(self):
        d = Path(tempfile.mkdtemp())
        try:
            report = chk.Report()
            chk.run_rule_c_tempo(d, {"command"}, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("SKIP: C" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


class RuleDDashboards(unittest.TestCase):
    def _run(self, json_text, l1, metric_labels=frozenset()):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "docker" / "telemetry" / "grafana" / "dashboards" / "x.json",
                json_text,
            )
            report = chk.Report()
            chk.run_rule_d_dashboards(d, set(l1), set(metric_labels), report)
            return sorted(v[2] for v in report.violations)
        finally:
            shutil.rmtree(d)

    def test_unknown_promql_label_flagged(self):
        self.assertEqual(
            self._run('"expr": "sum by (bogus_label) (x)"', {"command"}),
            ["bogus_label"],
        )

    def test_builtin_labels_not_flagged(self):
        self.assertEqual(
            self._run('"expr": "sum by (le, span_name, exported_instance) (x)"', set()),
            [],
        )

    def test_perf_iac_identity_labels_not_flagged(self):
        self.assertEqual(
            self._run('"expr": "sum by (xrpl_branch, xrpl_node_role) (x)"', set()),
            [],
        )

    def test_prometheus_name_label_not_flagged(self):
        # `__name__` is the Prometheus reserved metric-name label; the renamed
        # system-*.json dashboards use `sum by (le, __name__)`.
        self.assertEqual(
            self._run('"expr": "sum by (le, __name__) (rate(x[5m]))"', set()),
            [],
        )

    def test_l1_label_passes(self):
        self.assertEqual(self._run('"q": "{command=\\"x\\"}"', {"command"}), [])

    def test_traceql_span_prefix_stripped(self):
        # `span.establish_count` must validate against the bare L1 key.
        self.assertEqual(
            self._run(
                '"expr": "count_over_time(x) by (span.establish_count)"',
                {"establish_count"},
            ),
            [],
        )

    def test_traceql_resource_prefix_stripped(self):
        self.assertEqual(self._run('"q": "{resource.service_name=\\"x\\"}"', set()), [])

    def test_native_metric_label_passes(self):
        # `job_type` / `reason` are emitted by MetricsRegistry, not span attrs.
        self.assertEqual(
            self._run(
                '"expr": "sum by (job_type, reason) (x)"',
                {"command"},
                metric_labels={"job_type", "reason"},
            ),
            [],
        )

    def test_unknown_label_still_flagged_with_metric_labels(self):
        # A label that is neither L1, metric label, nor builtin still fails.
        self.assertEqual(
            self._run(
                '"expr": "sum by (bogus) (x)"',
                {"command"},
                metric_labels={"job_type"},
            ),
            ["bogus"],
        )

    def test_span_prefixed_unknown_still_flagged(self):
        # `span.not_a_key` whose bare form is unknown is still a violation.
        self.assertEqual(
            self._run('"expr": "x by (span.not_a_key)"', {"command"}),
            ["span.not_a_key"],
        )


class MetricLabelExtraction(unittest.TestCase):
    """L6: native-metric label keys parsed from C++ instrument calls."""

    def test_extracts_add_label(self):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricsRegistry.cpp",
                'counter->Add(1, {{"job_type", std::string(jobType)}});\n'
                'c2->Add(1, {{"reason", std::string(r)}});\n',
            )
            self.assertEqual(chk.metric_label_names(d), {"job_type", "reason"})
        finally:
            shutil.rmtree(d)

    def test_no_metrics_file_empty(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "src").mkdir()
            self.assertEqual(chk.metric_label_names(d), set())
        finally:
            shutil.rmtree(d)


class ReportExitContract(unittest.TestCase):
    @staticmethod
    def _exit_code(report):
        """Call render_and_exit (which prints + raises SystemExit), swallowing
        its stdout, and return the exit code."""
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                report.render_and_exit()
            except SystemExit as e:
                return e.code
        return None  # pragma: no cover - render_and_exit always exits

    def test_violation_exits_nonzero(self):
        r = chk.Report()
        r.violation("A", "f", "tok", "exp")
        self.assertEqual(self._exit_code(r), 1)

    def test_clean_exits_zero(self):
        r = chk.Report()
        r.ok("all good")
        self.assertEqual(self._exit_code(r), 0)

    def test_warning_only_exits_zero(self):
        r = chk.Report()
        r.warning("H", "f", "tok", "note")
        self.assertEqual(self._exit_code(r), 0)


class RuleEReportTuple(unittest.TestCase):
    """Assert Rule E records the full (rule, expected) tuple, not just token."""

    def test_violation_tuple_fields(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "docs").mkdir()
            (d / "docs" / "telemetry-runbook.md").write_text("`xrpl.tx.hash`")
            report = chk.Report()
            chk.run_rule_e_runbook(d, {"xrpl.network.id"}, report)
            self.assertEqual(len(report.violations), 1)
            rule, _loc, token, expected = report.violations[0]
            self.assertEqual(rule, "E")
            self.assertEqual(token, "xrpl.tx.hash")
            self.assertEqual(expected, "underscore, not dotted")
        finally:
            shutil.rmtree(d)

    def test_clean_runbook_records_ok(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "docs").mkdir()
            (d / "docs" / "telemetry-runbook.md").write_text(
                "`tx_hash` `consensus.round`"
            )
            report = chk.Report()
            chk.run_rule_e_runbook(d, {"tx_hash"}, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("E:" in c for c in report.checked))
        finally:
            shutil.rmtree(d)


if __name__ == "__main__":
    unittest.main(verbosity=2)
