#!/usr/bin/env python3

# cspell:ignore ISTOGRAM
# The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
# compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

"""Unit tests for check_otel_naming.py.

Stdlib-only (unittest), matching the dependency-free policy of the check itself.
Run from anywhere:

    python .github/scripts/otel-naming/test_check_otel_naming.py

Each rule is exercised in isolation against a synthetic tree / synthetic L1 key
set, covering positive (must flag), negative (must not flag), and boundary
cases. Rule E (doc-layer dotted-attribute detection) has the densest coverage
because its discriminator — the `xrpl.<domain>.` prefix vs span names,
filenames, OTel-standard keys, and metric labels — is the subtlest.
"""

import contextlib
import importlib.util
import io
import json
import shutil
import subprocess
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
    return _run_rule_e_docs({"docs/telemetry-runbook.md": runbook_text})[0]


def _run_rule_e_docs(docs, l1_keys=None):
    """Run Rule E against a synthetic doc set.

    `docs` maps a repo-relative path to that file's text; only the listed files
    are created, so per-file presence gating can be exercised. Returns
    (sorted flagged tokens, the Report) so location and skip/ok lines can be
    asserted as well as the tokens."""
    d = Path(tempfile.mkdtemp())
    try:
        for rel, text in docs.items():
            path = d / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        report = chk.Report()
        chk.run_rule_e_docs(d, set(L1) if l1_keys is None else set(l1_keys), report)
        return sorted(v[2] for v in report.violations), report
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

    def test_external_infra_dotted_resource_attrs_not_flagged(self):
        # perf-iac stamps these as dotted resource attrs (alloy pipeline);
        # EXTERNAL_INFRA_LABELS (Rule D) holds their underscore metric-label
        # form -- Rule E must also exempt the dotted resource-attr form.
        self.assertEqual(
            _run_rule_e("`xrpl.work.item` `xrpl.branch` `xrpl.node.role`"), []
        )

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
            chk.run_rule_e_docs(d, set(L1), report)
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
            chk.run_rule_e_docs(d, set(), report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("SKIP: E" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


# The doc paths the Rule E tests build synthetic trees from. All four are real,
# committable files that live under RULE_E_DOC_ROOTS, so a test tree has the same
# shape discovery meets in the repo. GLOSSARY and BUILD_GUIDE additionally prove
# discovery is not runbook-shaped: BUILD_GUIDE sits one directory deeper, so a
# non-recursive glob would miss it.
RUNBOOK_DOC = "docs/telemetry-runbook.md"
GLOSSARY_DOC = "docs/telemetry-glossary.md"
BUILD_GUIDE_DOC = "docs/build/telemetry.md"
TESTING_DOC = "docker/telemetry/TESTING.md"
ALL_DOCS = (RUNBOOK_DOC, GLOSSARY_DOC, BUILD_GUIDE_DOC, TESTING_DOC)

# Top-level trees that ship to the default branch, so anything the check is
# configured to read must live inside one of them. A configured path outside
# these would name a tree that is not part of the shipped repository: the rule
# would warn forever about a doc that can never appear, and the matching CI
# path-trigger would be dead weight.
COMMITTABLE_ROOTS = (Path("docs"), Path("docker"))


def _inside_any(rel: Path, roots) -> bool:
    """True if `rel` is one of `roots` or lives underneath one of them."""
    return any(root == rel or root in rel.parents for root in roots)


def _git_tracks(root: Path, rel: Path) -> bool:
    """True if git tracks `rel` (a file) or anything under it (a directory).

    Evidence that a configured path is genuinely part of the repository rather
    than a local scratch or ignored directory."""
    out = subprocess.run(
        ["git", "ls-files", "--", str(rel)],
        cwd=str(root),
        capture_output=True,
        text=True,
    )
    return bool(out.stdout.strip())


class RuleEDocSet(unittest.TestCase):
    """Rule E scans every discovered doc, not just the runbook. The glossary, the
    build/telemetry guide and the stack testing guide all publish attribute keys
    readers copy queries from, so a dotted key left in any of them is as
    operator-breaking as one in the runbook."""

    RUNBOOK = RUNBOOK_DOC
    GLOSSARY = GLOSSARY_DOC
    BUILD_GUIDE = BUILD_GUIDE_DOC
    TESTING = TESTING_DOC

    def test_flags_dotted_attr_in_testing_guide(self):
        # TESTING.md publishes copy-paste TraceQL/PromQL, so a dotted key there
        # hands the reader a query that silently matches nothing.
        tokens, report = _run_rule_e_docs(
            {self.TESTING: '{name="tx.process" && span.`xrpl.tx.hash`="AB"}'}
        )
        self.assertEqual(tokens, ["xrpl.tx.hash"])
        self.assertEqual(report.violations[0][1], f"{self.TESTING}:1")
        self.assertEqual(report.violations[0][3], "underscore, not dotted")

    def test_testing_guide_checked_when_every_other_doc_absent(self):
        # Per-file presence gating: TESTING.md alone must not skip the rule.
        tokens, report = _run_rule_e_docs({self.TESTING: "`xrpl.peer.version`"})
        self.assertEqual(tokens, ["xrpl.peer.version"])
        self.assertEqual(report.skips, [])
        self.assertEqual(report.violations[0][1], f"{self.TESTING}:1")
        # A violating run emits the violation, not an "ok" line.
        self.assertEqual(report.checked, [])

    def test_testing_guide_alone_reports_one_clean_file(self):
        tokens, report = _run_rule_e_docs({self.TESTING: "`tx_hash`"})
        self.assertEqual(tokens, [])
        self.assertTrue(any("E:" in c and "1 file(s)" in c for c in report.checked))

    def test_clean_testing_guide_passes_with_bare_keys(self):
        # The real docker/telemetry/TESTING.md shape: bare/underscore attr keys
        # plus dotted SPAN names, which must not be flagged.
        tokens, report = _run_rule_e_docs(
            {self.TESTING: '{name="tx.process" && span.tx_hash!=""} `ledger_seq`'}
        )
        self.assertEqual(tokens, [])
        self.assertEqual(report.violations, [])

    def test_exemptions_apply_in_testing_guide_too(self):
        tokens, _ = _run_rule_e_docs(
            {self.TESTING: "`xrpl.network.type` `xrpl.work.item`"}
        )
        self.assertEqual(tokens, [])

    def test_flags_dotted_attr_in_glossary(self):
        tokens, report = _run_rule_e_docs({self.GLOSSARY: "| `xrpl.tx.hash` | h |"})
        self.assertEqual(tokens, ["xrpl.tx.hash"])
        self.assertEqual(report.violations[0][1], f"{self.GLOSSARY}:1")

    def test_flags_dotted_attr_in_nested_build_guide(self):
        # One directory below the root: proves discovery recurses rather than
        # globbing only the root's own children.
        tokens, report = _run_rule_e_docs({self.BUILD_GUIDE: "`xrpl.consensus.mode`"})
        self.assertEqual(tokens, ["xrpl.consensus.mode"])
        self.assertEqual(report.violations[0][1], f"{self.BUILD_GUIDE}:1")

    def test_location_carries_the_offending_line_number(self):
        tokens, report = _run_rule_e_docs(
            {self.GLOSSARY: "clean\nstill clean\n| `xrpl.tx.hash` |"}
        )
        self.assertEqual(tokens, ["xrpl.tx.hash"])
        self.assertEqual(report.violations[0][1], f"{self.GLOSSARY}:3")

    def test_flags_across_all_four_docs(self):
        tokens, report = _run_rule_e_docs(
            {
                self.RUNBOOK: "`xrpl.tx.hash`",
                self.GLOSSARY: "`xrpl.peer.id`",
                self.BUILD_GUIDE: "`xrpl.ledger.seq`",
                self.TESTING: "`xrpl.rpc.command`",
            }
        )
        self.assertEqual(
            tokens,
            [
                "xrpl.ledger.seq",
                "xrpl.peer.id",
                "xrpl.rpc.command",
                "xrpl.tx.hash",
            ],
        )
        self.assertEqual(
            sorted(v[1] for v in report.violations),
            sorted(
                [
                    f"{self.RUNBOOK}:1",
                    f"{self.GLOSSARY}:1",
                    f"{self.BUILD_GUIDE}:1",
                    f"{self.TESTING}:1",
                ]
            ),
        )

    def test_other_doc_checked_when_runbook_absent(self):
        # Presence gating is per discovered file: a branch without the runbook
        # still gets the docs it does carry checked, instead of the rule skipping.
        tokens, report = _run_rule_e_docs({self.GLOSSARY: "`xrpl.tx.hash`"})
        self.assertEqual(tokens, ["xrpl.tx.hash"])
        self.assertEqual(report.skips, [])

    def test_absent_doc_is_not_an_error(self):
        tokens, report = _run_rule_e_docs({self.RUNBOOK: "`tx_hash`"})
        self.assertEqual(tokens, [])
        self.assertTrue(any("1 file(s)" in c for c in report.checked))

    def test_clean_doc_set_reports_file_count(self):
        tokens, report = _run_rule_e_docs(
            {
                self.RUNBOOK: "`tx_hash` `consensus.round`",
                self.GLOSSARY: "`xrpl.network.id` `ledger_seq`",
                self.BUILD_GUIDE: "`service.name` `peer_id`",
                self.TESTING: "`rpc_command` `tx.process`",
            }
        )
        self.assertEqual(tokens, [])
        self.assertEqual(report.violations, [])
        self.assertTrue(any("E:" in c and "4 file(s)" in c for c in report.checked))

    def test_skips_when_no_doc_present(self):
        tokens, report = _run_rule_e_docs({})
        self.assertEqual(tokens, [])
        self.assertEqual(report.checked, [])
        self.assertTrue(any("SKIP: E" in s for s in report.skips))

    def test_exemptions_apply_in_every_discovered_doc(self):
        # The L1 resource attrs and perf-iac dotted identities must stay exempt
        # in every discovered doc, exactly as in the runbook.
        tokens, _ = _run_rule_e_docs(
            {
                self.GLOSSARY: "`xrpl.network.id` `xrpl.network.type`",
                self.BUILD_GUIDE: "`xrpl.work.item` `xrpl.branch` `xrpl.node.role`",
            }
        )
        self.assertEqual(tokens, [])


def _marker(*keys: str) -> str:
    """Build an allow-dotted marker naming `keys` (no key = the bare form)."""
    body = ": " + ", ".join(keys) if keys else ""
    return f"<!-- otel-naming:allow-dotted{body} -->"


def _marker_warnings(report):
    """The marker-related warnings only.

    A synthetic doc set that does not create the runbook leaves that required
    anchor absent, which is warned about in its own right (see
    RuleERequiredDocPresence). Filtering those out keeps the marker tests
    asserting the marker's own behaviour."""
    return [tuple(w) for w in report.warnings if w[2] != "absent"]


class RuleEAllowDottedMarkerParsing(unittest.TestCase):
    """`rule_e_allowed_keys` — the marker parser. The key list is what bounds the
    exemption, so its parsing (and the marker COUNT, which distinguishes "no
    marker" from "a marker that names nothing") is asserted directly."""

    def test_no_marker_yields_no_keys_and_no_marker_count(self):
        self.assertEqual(
            chk.rule_e_allowed_keys("plain `xrpl.tx.hash` line"), (set(), 0)
        )

    def test_bare_marker_counts_but_names_no_key(self):
        self.assertEqual(chk.rule_e_allowed_keys(_marker()), (set(), 1))

    def test_single_key(self):
        self.assertEqual(
            chk.rule_e_allowed_keys(_marker("xrpl.tx.hash")), ({"xrpl.tx.hash"}, 1)
        )

    def test_comma_separated_keys(self):
        self.assertEqual(
            chk.rule_e_allowed_keys(_marker("xrpl.tx.hash", "xrpl.node.server_state")),
            ({"xrpl.tx.hash", "xrpl.node.server_state"}, 1),
        )

    def test_space_separated_and_backticked_keys(self):
        self.assertEqual(
            chk.rule_e_allowed_keys(
                "<!-- otel-naming:allow-dotted: `xrpl.tx.hash` `xrpl.peer.id` -->"
            ),
            ({"xrpl.tx.hash", "xrpl.peer.id"}, 1),
        )

    def test_no_surrounding_whitespace(self):
        self.assertEqual(
            chk.rule_e_allowed_keys("<!--otel-naming:allow-dotted:xrpl.tx.hash-->"),
            ({"xrpl.tx.hash"}, 1),
        )

    def test_two_markers_on_one_line_union(self):
        line = _marker("xrpl.tx.hash") + " text " + _marker("xrpl.peer.id")
        self.assertEqual(
            chk.rule_e_allowed_keys(line), ({"xrpl.tx.hash", "xrpl.peer.id"}, 2)
        )

    def test_prose_around_marker_is_not_absorbed_as_a_key(self):
        line = "use `tx_hash`, not `xrpl.tx.hash`. " + _marker("xrpl.tx.hash") + " ok"
        self.assertEqual(chk.rule_e_allowed_keys(line), ({"xrpl.tx.hash"}, 1))

    def test_trailing_comma_does_not_yield_empty_key(self):
        self.assertEqual(
            chk.rule_e_allowed_keys("<!-- otel-naming:allow-dotted: xrpl.tx.hash, -->"),
            ({"xrpl.tx.hash"}, 1),
        )


class RuleEAllowDottedMarkerEnforcement(unittest.TestCase):
    """The marker exempts ONLY the keys it names, only on its own line. A
    blanket line opt-out would let a genuine violation appended to any marked
    line ride in silently, which is exactly what these tests forbid."""

    REFERENCE = GLOSSARY_DOC

    def test_named_key_is_exempt(self):
        tokens, report = _run_rule_e_docs(
            {
                self.REFERENCE: "use `tx_hash`, not `xrpl.tx.hash`. "
                + _marker("xrpl.tx.hash")
            }
        )
        self.assertEqual(tokens, [])
        self.assertEqual(report.violations, [])
        self.assertEqual(_marker_warnings(report), [])

    def test_unlisted_key_on_marked_line_still_fails(self):
        # The hole this closes: one marker must not cover a second dotted key
        # someone appends to the line later.
        tokens, report = _run_rule_e_docs(
            {
                self.REFERENCE: "not `xrpl.tx.hash` and also `xrpl.peer.id` "
                + _marker("xrpl.tx.hash")
            }
        )
        self.assertEqual(tokens, ["xrpl.peer.id"])
        self.assertEqual(report.violations[0][0], "E")
        self.assertEqual(report.violations[0][1], f"{self.REFERENCE}:1")
        self.assertEqual(report.violations[0][3], "underscore, not dotted")

    def test_bare_marker_exempts_nothing_and_warns(self):
        tokens, report = _run_rule_e_docs(
            {self.REFERENCE: "not `xrpl.tx.hash`. " + _marker()}
        )
        self.assertEqual(tokens, ["xrpl.tx.hash"])
        self.assertEqual(report.violations[0][1], f"{self.REFERENCE}:1")
        warnings = _marker_warnings(report)
        self.assertEqual(
            [w[:3] for w in warnings], [("E", f"{self.REFERENCE}:1", "allow-dotted")]
        )
        self.assertIn(chk.RULE_E_MARKER_SYNTAX, warnings[0][3])

    def test_marker_does_not_leak_to_adjacent_lines(self):
        tokens, report = _run_rule_e_docs(
            {
                self.REFERENCE: "| `xrpl.tx.hash` | before |\n"
                + "not `xrpl.tx.hash`. "
                + _marker("xrpl.tx.hash")
                + "\n| `xrpl.tx.hash` | after |"
            }
        )
        self.assertEqual(tokens, ["xrpl.tx.hash", "xrpl.tx.hash"])
        self.assertEqual(
            sorted(v[1] for v in report.violations),
            [f"{self.REFERENCE}:1", f"{self.REFERENCE}:3"],
        )

    def test_all_named_keys_exempt_on_a_table_row(self):
        # The real-world shape: a "was -> is now" row naming two old
        # dotted keys, with both listed in the marker.
        tokens, _ = _run_rule_e_docs(
            {
                self.REFERENCE: "| `xrpl.validation.full`, `xrpl.peer.validation.full`"
                " | one bare `full_validation` | "
                + _marker("xrpl.validation.full", "xrpl.peer.validation.full")
            }
        )
        self.assertEqual(tokens, [])

    def test_partially_listed_table_row_fails_on_the_unlisted_key(self):
        tokens, report = _run_rule_e_docs(
            {
                self.REFERENCE: "| `xrpl.validation.full`, `xrpl.peer.validation.full`"
                " | one bare `full_validation` | " + _marker("xrpl.validation.full")
            }
        )
        self.assertEqual(tokens, ["xrpl.peer.validation.full"])
        self.assertEqual(report.violations[0][1], f"{self.REFERENCE}:1")

    def test_key_prefix_does_not_exempt_a_longer_key(self):
        # Exemption is an exact token match, not a prefix match.
        tokens, _ = _run_rule_e_docs(
            {self.REFERENCE: "`xrpl.tx.hash` " + _marker("xrpl.tx")}
        )
        self.assertEqual(tokens, ["xrpl.tx.hash"])

    def test_dotted_prefix_token_can_be_listed_verbatim(self):
        # The docs grep for prefixes such as `xrpl.node.` (trailing dot); the
        # marker must accept that exact token.
        tokens, report = _run_rule_e_docs(
            {
                self.REFERENCE: "a grep for `xrpl.node.` and `xrpl.peer.` returns "
                "nothing " + _marker("xrpl.node.", "xrpl.peer.")
            }
        )
        self.assertEqual(tokens, [])
        self.assertEqual(_marker_warnings(report), [])

    def test_stale_key_warns_but_does_not_fail(self):
        tokens, report = _run_rule_e_docs(
            {self.REFERENCE: "all clean now. " + _marker("xrpl.tx.hash")}
        )
        self.assertEqual(tokens, [])
        self.assertEqual(report.violations, [])
        self.assertEqual(
            _marker_warnings(report),
            [
                (
                    "E",
                    f"{self.REFERENCE}:1",
                    "xrpl.tx.hash",
                    "allow-dotted key not on this line",
                )
            ],
        )

    def test_marker_naming_an_l1_key_is_not_reported_stale(self):
        # `xrpl.network.id` is exempt via L1, so a marker naming it is redundant
        # but not stale — the key IS on the line.
        tokens, report = _run_rule_e_docs(
            {self.REFERENCE: "`xrpl.network.id` " + _marker("xrpl.network.id")}
        )
        self.assertEqual(tokens, [])
        self.assertEqual(_marker_warnings(report), [])

    def test_marker_works_in_every_doc_of_the_set(self):
        docs = {
            rel: "not `xrpl.tx.hash`. " + _marker("xrpl.tx.hash") for rel in ALL_DOCS
        }
        tokens, report = _run_rule_e_docs(docs)
        self.assertEqual(tokens, [])
        self.assertEqual(report.violations, [])


class RuleEDocDiscovery(unittest.TestCase):
    """`rule_e_docs` — the discovery that replaced a hardcoded doc list. What has
    to hold: it finds the real docs in this repo (a rule scanning an empty set
    passes without checking anything), it recurses into subdirectories, it never
    checks a file twice, and every path it is configured with lies inside a tree
    that actually ships."""

    def test_discovery_finds_more_than_one_doc_in_this_repo(self):
        # The vacuous-pass guard, measured against the real tree: one file (or
        # none) would mean discovery had collapsed and Rule E was reporting green
        # over almost nothing.
        found = chk.rule_e_docs(chk.repo_root())
        self.assertGreater(len(found), 1, found)

    def test_discovery_returns_no_duplicates(self):
        # Roots may overlap (a root and a subdirectory of it); a file must still
        # be checked once, so line numbers are not reported twice.
        found = chk.rule_e_docs(chk.repo_root())
        self.assertEqual(len(found), len(set(found)))

    def test_discovery_finds_the_known_telemetry_docs(self):
        # Named here as an expectation of DISCOVERY, not as the checker's config:
        # if any of these is renamed, discovery still covers it under its new
        # name and only this assertion needs updating.
        found = set(chk.rule_e_docs(chk.repo_root()))
        for rel in (RUNBOOK_DOC, GLOSSARY_DOC, TESTING_DOC):
            self.assertIn(Path(rel), found)

    def test_discovery_recurses_into_subdirectories(self):
        d = Path(tempfile.mkdtemp())
        try:
            nested = d / "docs" / "build" / "deep"
            nested.mkdir(parents=True)
            (nested / "telemetry.md").write_text("`tx_hash`")
            self.assertEqual(
                chk.rule_e_docs(d), [Path("docs") / "build" / "deep" / "telemetry.md"]
            )
        finally:
            shutil.rmtree(d)

    def test_discovery_ignores_non_markdown(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "docs").mkdir()
            (d / "docs" / "notes.txt").write_text("`xrpl.tx.hash`")
            (d / "docs" / "runbook.md").write_text("`tx_hash`")
            self.assertEqual(chk.rule_e_docs(d), [Path("docs") / "runbook.md"])
        finally:
            shutil.rmtree(d)

    def test_discovery_tolerates_an_absent_root(self):
        # Presence gating applies to the roots too: a tree carrying only one of
        # them must not raise, and must still discover that one.
        d = Path(tempfile.mkdtemp())
        try:
            (d / "docker" / "telemetry").mkdir(parents=True)
            (d / "docker" / "telemetry" / "TESTING.md").write_text("`tx_hash`")
            self.assertEqual(
                chk.rule_e_docs(d), [Path("docker") / "telemetry" / "TESTING.md"]
            )
        finally:
            shutil.rmtree(d)

    def test_no_configured_path_lies_outside_the_committable_roots(self):
        # The check may only be wired to trees that ship. A configured path
        # outside them would make Rule E warn about a doc that can never appear
        # and leave the matching CI path-trigger dead.
        for rel in chk.RULE_E_DOC_ROOTS + chk.RULE_E_REQUIRED_DOCS:
            self.assertTrue(_inside_any(rel, COMMITTABLE_ROOTS), rel)

    def test_configured_paths_exist_and_are_version_controlled(self):
        # "Committable" is not just a naming claim: git must actually track the
        # contents of every configured path in this repo.
        root = chk.repo_root()
        for rel in chk.RULE_E_DOC_ROOTS:
            self.assertTrue((root / rel).is_dir(), rel)
            self.assertTrue(_git_tracks(root, rel), rel)
        for rel in chk.RULE_E_REQUIRED_DOCS:
            self.assertTrue((root / rel).is_file(), rel)
            self.assertTrue(_git_tracks(root, rel), rel)

    def test_every_required_doc_is_discoverable(self):
        # An anchor outside every root could never be found, so the tripwire
        # would fire on every run and stop meaning anything.
        for rel in chk.RULE_E_REQUIRED_DOCS:
            self.assertTrue(_inside_any(rel, chk.RULE_E_DOC_ROOTS), rel)
            self.assertIn(rel, chk.rule_e_docs(chk.repo_root()))


class RuleERequiredDocPresence(unittest.TestCase):
    """A RULE_E_REQUIRED_DOCS anchor that is not in the tree must be VISIBLE.
    Discovery cannot go stale, but it can go quiet — a root emptied of telemetry
    docs still yields a clean run — so the anchor's absence is warned about and
    counted, while staying non-fatal so a partial branch still passes."""

    RUNBOOK = RUNBOOK_DOC

    def test_every_required_doc_resolves_in_this_repo(self):
        # If the runbook is renamed and RULE_E_REQUIRED_DOCS is not updated, the
        # anchor warning would fire on every run; catch it here instead.
        root = chk.repo_root()
        self.assertEqual(
            [
                str(rel)
                for rel in chk.RULE_E_REQUIRED_DOCS
                if not (root / rel).is_file()
            ],
            [],
        )

    def test_missing_anchor_is_warned_once(self):
        # Discovery finds the glossary, so the rule runs — but the anchor it is
        # required to find is gone, which must be said out loud.
        tokens, report = _run_rule_e_docs({GLOSSARY_DOC: "`tx_hash`"})
        self.assertEqual(tokens, [])
        self.assertEqual(
            [tuple(w) for w in report.warnings],
            [
                (
                    "E",
                    self.RUNBOOK,
                    "absent",
                    "required Rule E doc not in tree (renamed?)",
                )
            ],
        )

    def test_ok_line_names_the_absent_anchor_count(self):
        tokens, report = _run_rule_e_docs({GLOSSARY_DOC: "`tx_hash`"})
        self.assertEqual(tokens, [])
        self.assertTrue(
            any(
                "1 file(s) checked" in c
                and f"1 of {len(chk.RULE_E_REQUIRED_DOCS)} required doc(s) absent" in c
                for c in report.checked
            ),
            report.checked,
        )

    def test_full_doc_set_warns_nothing_and_reports_the_full_count(self):
        docs = {rel: "`tx_hash`" for rel in ALL_DOCS}
        tokens, report = _run_rule_e_docs(docs)
        self.assertEqual(tokens, [])
        self.assertEqual(report.warnings, [])
        self.assertTrue(
            any(
                f"{len(ALL_DOCS)} file(s) checked" in c and "absent" not in c
                for c in report.checked
            ),
            report.checked,
        )

    def test_anchor_present_warns_nothing_even_with_other_docs_absent(self):
        # Only the ANCHOR is required. A tree carrying just the runbook is a
        # legitimate partial branch, not a shrinking doc set.
        tokens, report = _run_rule_e_docs({self.RUNBOOK: "`tx_hash`"})
        self.assertEqual(tokens, [])
        self.assertEqual(report.warnings, [])
        self.assertTrue(
            any("1 file(s) checked" in c and "absent" not in c for c in report.checked),
            report.checked,
        )

    def test_absent_anchor_is_warned_even_when_l1_is_empty(self):
        # The anchor can be verified without an L1 key set, so the visibility
        # signal must not depend on the (separately gated) key comparison.
        tokens, report = _run_rule_e_docs({GLOSSARY_DOC: "`xrpl.tx.hash`"}, l1_keys=[])
        self.assertEqual(tokens, [])
        self.assertEqual(len(report.warnings), len(chk.RULE_E_REQUIRED_DOCS))
        self.assertTrue(any("SKIP: E" in s for s in report.skips))

    def test_no_anchor_warning_when_discovery_finds_nothing(self):
        # Nothing to check at all is already reported by the SKIP line, which
        # names the roots it searched, so an anchor warning would be noise.
        tokens, report = _run_rule_e_docs({})
        self.assertEqual(tokens, [])
        self.assertEqual(report.warnings, [])
        roots = ", ".join(str(rel) for rel in chk.RULE_E_DOC_ROOTS)
        self.assertEqual(
            report.skips,
            [f"SKIP: E — no doc-layer file present (no *.md under {roots})"],
        )

    def test_doc_outside_every_root_is_not_discovered(self):
        # The counterpart of the roots being committable: markdown parked outside
        # them is not part of the L5 layer, so a dotted key there cannot fail the
        # build — and equally cannot make the rule look like it checked something.
        tokens, report = _run_rule_e_docs({"elsewhere/notes.md": "`xrpl.tx.hash`"})
        self.assertEqual(tokens, [])
        self.assertEqual(report.checked, [])
        self.assertTrue(any("SKIP: E" in s for s in report.skips))


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


class RuleMUnreferencedConstants(unittest.TestCase):
    """Rule M: an L1 constant defined in a `*SpanNames.h` that nothing in
    `src/**` or `include/**` references."""

    # The two notes Rule M distinguishes; they must not be folded together.
    DEAD = "no reference in src/ or include/"
    TEST_ONLY = "referenced only by test code"

    def _run(self, files):
        """Build a synthetic tree, run Rule M, return (sorted (token, note)
        pairs, the Report)."""
        d = Path(tempfile.mkdtemp())
        try:
            for rel, text in files.items():
                _write(d / rel, text)
            report = chk.Report()
            chk.run_rule_m_unreferenced(d, chk.find_spanname_headers(d), report)
            return sorted((w[2], w[3]) for w in report.warnings), report
        finally:
            shutil.rmtree(d)

    HEADER = "include/xrpl/telemetry/DemoSpanNames.h"

    def _two_attrs(self):
        return _header(
            'inline constexpr auto used = makeStr("used_key");\n'
            'inline constexpr auto unused = makeStr("unused_key");\n'
        )

    # ----- positive: a defined-but-unreferenced constant is warned -----
    def test_unreferenced_constant_warned(self):
        warnings, _ = self._run(
            {
                self.HEADER: self._two_attrs(),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
            }
        )
        self.assertEqual(warnings, [("attr::unused", self.DEAD)])

    def test_warning_location_is_the_defining_header(self):
        _, report = self._run(
            {
                self.HEADER: self._two_attrs(),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
            }
        )
        self.assertEqual(len(report.warnings), 1)
        self.assertEqual(report.warnings[0][0], "M")
        self.assertEqual(report.warnings[0][1], self.HEADER)

    def test_lone_header_flags_every_constant(self):
        # Proves the definition itself is not counted as a reference: with no
        # other file in the tree, BOTH constants are dead.
        warnings, _ = self._run({self.HEADER: self._two_attrs()})
        self.assertEqual(
            warnings, [("attr::unused", self.DEAD), ("attr::used", self.DEAD)]
        )

    def test_comment_mention_is_not_a_reference(self):
        warnings, _ = self._run(
            {
                self.HEADER: self._two_attrs(),
                "src/xrpld/app/Foo.cpp": (
                    "g.setAttribute(demo::attr::used, v);\n"
                    "// see demo::attr::unused for the aborted case\n"
                    "/* demo::attr::unused */\n"
                ),
            }
        )
        self.assertEqual(warnings, [("attr::unused", self.DEAD)])

    def test_using_reexport_is_not_a_reference(self):
        # An unused `using` re-export must not vouch for the constant it
        # renames. Fails if referenced_constants stops stripping USING_DECL.
        warnings, _ = self._run(
            {
                self.HEADER: self._two_attrs(),
                "include/xrpl/telemetry/PeerSpanNames.h": _header(
                    "using ::xrpl::telemetry::demo::span::attr::unused;\n"
                ),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
            }
        )
        self.assertEqual(warnings, [("attr::unused", self.DEAD)])

    # ----- negative: a referenced constant must NOT be warned -----
    def test_referenced_constant_not_warned(self):
        warnings, report = self._run(
            {
                self.HEADER: _header('inline constexpr auto used = makeStr("k");\n'),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
            }
        )
        self.assertEqual(warnings, [])
        self.assertTrue(any("M:" in c for c in report.checked))

    def test_reference_as_an_attribute_value_counts(self):
        # Value constants are referenced in the VALUE position, which a scan of
        # key positions only would never see.
        warnings, _ = self._run(
            {
                self.HEADER: _header(
                    'inline constexpr auto outcome = makeStr("outcome");\n'
                    'inline constexpr auto complete = makeStr("complete");\n'
                ),
                "src/xrpld/app/Foo.cpp": (
                    "g.setAttribute(demo::attr::outcome, demo::attr::complete);\n"
                ),
            }
        )
        self.assertEqual(warnings, [])

    def test_reference_through_a_local_counts(self):
        # Constants are passed to helpers and stored in locals, not only used
        # inline at a telemetry call.
        warnings, _ = self._run(
            {
                self.HEADER: _header('inline constexpr auto used = makeStr("k");\n'),
                "src/xrpld/app/Foo.cpp": (
                    "auto key = demo::attr::used;\nrecord(key, v);\n"
                ),
            }
        )
        self.assertEqual(warnings, [])

    def test_reference_from_a_join_in_its_own_header_counts(self):
        # `op::leaf` is consumed only by a `join(...)` in the same header; the
        # composed constant is what the call site names. Neither is dead.
        warnings, _ = self._run(
            {
                self.HEADER: (
                    "#pragma once\n"
                    "namespace demo {\n"
                    "namespace seg {\n"
                    'inline constexpr auto demo = makeStr("demo");\n'
                    "}\n"
                    "namespace op {\n"
                    'inline constexpr auto leaf = makeStr("leaf");\n'
                    "}\n"
                    "namespace span {\n"
                    "inline constexpr auto full = join(seg::demo, op::leaf);\n"
                    "}\n"
                    "}\n"
                ),
                "src/xrpld/app/Foo.cpp": "auto s = g.childSpan(demo::span::full);\n",
            }
        )
        self.assertEqual(warnings, [])

    # ----- test-only references are a separate, named class -----
    def test_test_only_reference_reported_separately(self):
        warnings, _ = self._run(
            {
                self.HEADER: self._two_attrs(),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
                "src/tests/libxrpl/telemetry/SpanNames.cpp": (
                    'EXPECT_EQ(demo::attr::unused, "unused_key");\n'
                ),
            }
        )
        self.assertEqual(warnings, [("attr::unused", self.TEST_ONLY)])

    def test_production_reference_wins_over_test_reference(self):
        warnings, _ = self._run(
            {
                self.HEADER: _header('inline constexpr auto used = makeStr("k");\n'),
                "src/xrpld/app/Foo.cpp": "g.setAttribute(demo::attr::used, v);\n",
                "src/tests/libxrpl/telemetry/SpanNames.cpp": (
                    'EXPECT_EQ(demo::attr::used, "k");\n'
                ),
            }
        )
        self.assertEqual(warnings, [])

    # ----- boundary -----
    def test_skips_when_no_headers(self):
        d = Path(tempfile.mkdtemp())
        try:
            report = chk.Report()
            chk.run_rule_m_unreferenced(d, [], report)
            self.assertEqual(report.warnings, [])
            self.assertTrue(any("SKIP: M" in s for s in report.skips))
        finally:
            shutil.rmtree(d)

    def test_never_fails_the_build(self):
        _, report = self._run({self.HEADER: self._two_attrs()})
        self.assertEqual(report.violations, [])


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

    def test_external_infra_labels_not_flagged(self):
        # EXTERNAL_INFRA_LABELS (perf-iac identity labels with no in-tree
        # source) must be recognized as valid, distinct from `builtins`.
        expr = "sum by (" + ", ".join(sorted(chk.EXTERNAL_INFRA_LABELS)) + ") (x)"
        self.assertEqual(self._run(f'"expr": "{expr}"', set()), [])

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

    # ----- log-datasource (Loki/LogQL) exemption -----

    @staticmethod
    def _dashboard(panels):
        """Serialize a minimal well-formed dashboard around the given panels."""
        return json.dumps({"panels": panels})

    @staticmethod
    def _panel(ds_type, expr):
        return {
            "type": "timeseries",
            "datasource": {"type": ds_type, "uid": "U1"},
            "targets": [{"datasource": {"type": ds_type, "uid": "U1"}, "expr": expr}],
        }

    def test_loki_query_labels_exempt(self):
        # LogQL labels come from a collector regex_parser capture or an in-query
        # `| regexp` stage, so they have no L1/L6 source and must not be flagged.
        expr = 'sum by (partition, severity) ({service_name="xrpld"} | regexp `(?P<state>\\w+)`)'
        self.assertEqual(
            self._run(self._dashboard([self._panel("loki", expr)]), {"command"}), []
        )

    def test_prometheus_query_still_checked_in_same_file(self):
        # The exemption is per query, not per file: a Prometheus panel beside a
        # Loki one is still validated.
        panels = [
            self._panel("loki", 'sum by (partition) ({service_name="xrpld"})'),
            self._panel("prometheus", "sum by (bogus_label) (rate(x[5m]))"),
        ]
        self.assertEqual(
            self._run(self._dashboard(panels), {"command"}), ["bogus_label"]
        )

    def test_target_inherits_panel_datasource(self):
        # A target with no datasource of its own inherits the panel's, so its
        # LogQL labels are still exempt.
        panel = {
            "type": "timeseries",
            "datasource": {"type": "loki", "uid": "U1"},
            "targets": [{"expr": 'sum by (partition) ({service_name="xrpld"})'}],
        }
        self.assertEqual(self._run(self._dashboard([panel]), {"command"}), [])

    def test_loki_exemption_does_not_leak_to_sibling_panel(self):
        # A Loki panel must not make a later panel's datasource-less query
        # exempt -- inheritance flows down the tree, never sideways.
        panels = [
            self._panel("loki", 'sum by (partition) ({service_name="xrpld"})'),
            {"type": "timeseries", "targets": [{"expr": "sum by (leaked_label) (x)"}]},
        ]
        self.assertEqual(
            self._run(self._dashboard(panels), {"command"}), ["leaked_label"]
        )

    def test_nested_row_panel_is_walked(self):
        # Panels nested inside a collapsed row must still be checked.
        row = {
            "type": "row",
            "panels": [self._panel("prometheus", "sum by (nested_bogus) (x)")],
        }
        self.assertEqual(
            self._run(self._dashboard([row]), {"command"}), ["nested_bogus"]
        )

    def test_traceql_intrinsics_not_flagged(self):
        # `name` is the span-name intrinsic and `resource.service.instance.id`
        # strips to a dotted service-identity key; neither is in *SpanNames.h.
        expr = '{name="consensus.accept" && resource.service.instance.id=~"$node"}'
        self.assertEqual(
            self._run(self._dashboard([self._panel("tempo", expr)]), {"command"}), []
        )

    def test_malformed_json_falls_back_to_raw_scan(self):
        # A fragment cannot be attributed to a datasource, so every query is
        # checked rather than silently skipped.
        self.assertEqual(
            self._run('"expr": "sum by (fragment_bogus) (x)"', {"command"}),
            ["fragment_bogus"],
        )


class PromotedResourceLabels(unittest.TestCase):
    """L2: resource attrs the collector promotes onto metric datapoints."""

    def _run(self, collector_text):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "docker" / "telemetry" / "otel-collector-config.yaml",
                collector_text,
            )
            return chk.promoted_resource_labels(d)
        finally:
            shutil.rmtree(d)

    def test_both_forms_returned(self):
        # The prometheus exporter rewrites dots to underscores, and TraceQL uses
        # the dotted form, so a listed key must validate either way.
        labels = self._run(
            "    resource_metrics_key_attributes:\n"
            "      - service.instance.id\n"
            "      - deployment.environment\n"
            "      - xrpl.network.type\n"
        )
        self.assertIn("deployment_environment", labels)
        self.assertIn("deployment.environment", labels)
        self.assertIn("xrpl_network_type", labels)

    def test_block_ends_at_next_key(self):
        # A sibling key ends the list; its values must not become labels.
        labels = self._run(
            "    resource_metrics_key_attributes:\n"
            "      - deployment.environment\n"
            "    histogram:\n"
            "      unit: ms\n"
        )
        self.assertIn("deployment_environment", labels)
        self.assertNotIn("unit", labels)
        self.assertNotIn("ms", labels)

    def test_absent_config_returns_empty(self):
        # Presence-gated, like every other rule input.
        self.assertEqual(
            chk.promoted_resource_labels(Path(tempfile.gettempdir()) / "nope"), set()
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

    def test_extracts_second_label_of_a_pair(self):
        """A multi-label map opens the first pair with `{{` and later ones with
        a single `{`; every key must be collected, not just the first."""
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricsRegistry.cpp",
                'h->Record(v, {{"job_type", std::string(t)}, {"handler", h2}});\n',
            )
            self.assertEqual(chk.metric_label_names(d), {"job_type", "handler"})
        finally:
            shutil.rmtree(d)

    def test_resolves_label_key_constants(self):
        """A key hoisted into a `constexpr char k...[]` constant resolves to its
        string, including when the constant is declared in a header."""
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "include" / "xrpl" / "telemetry" / "GetObjectMetricNames.h",
                "// metric name constants\n"
                'inline constexpr char kLabelResult[] = "result";\n',
            )
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricsRegistry.cpp",
                'constexpr char kHandlerLabel[] = "handler";\n'
                "counter->Add(1, {{kLabelResult, std::string(r)}});\n"
                'c2->Add(1, {{"job_type", std::string(t)}, {kHandlerLabel, h}});\n',
            )
            self.assertEqual(
                chk.metric_label_names(d), {"result", "handler", "job_type"}
            )
        finally:
            shutil.rmtree(d)

    def test_ignores_plain_brace_initializers(self):
        """Ordinary brace lists are not label maps: a bare `{"http", "https"}`
        array must not license a dashboard filter on `http`."""
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricsRegistry.cpp",
                'std::array schemes{"http", "https", "ws"};\n'
                'for (auto const* k : {"read_request_bundle", "read_threads"})\n'
                "    use(k);\n"
                'counter->Add(1, {{"job_type", std::string(t)}});\n',
            )
            self.assertEqual(chk.metric_label_names(d), {"job_type"})
        finally:
            shutil.rmtree(d)

    def test_ignores_test_code_literals(self):
        """Test fixtures pass arbitrary literal pairs; they define no labels."""
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "test" / "jtx" / "Env_test.cpp",
                "// metric\n" 'env(signers("alice", 1, {{"alice", 1}, {"bob", 2}}));\n',
            )
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
            chk.run_rule_e_docs(d, {"xrpl.network.id"}, report)
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
            chk.run_rule_e_docs(d, {"tx_hash"}, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("E:" in c for c in report.checked))
        finally:
            shutil.rmtree(d)


# A minimal *MetricNames.h body: the three namespaces the metric rules read.
def _metric_header(
    metric_body: str = "", label_body: str = "", lval_body: str = ""
) -> str:
    return (
        "#pragma once\n"
        "namespace xrpl::telemetry {\n"
        f"namespace metric {{\n{metric_body}\n}}\n"
        f"namespace label {{\n{label_body}\n}}\n"
        f"namespace lval {{\n{lval_body}\n}}\n"
        "}\n"
    )


def _mc(symbol: str, wire: str) -> str:
    """One `inline constexpr char SYM[] = "wire";` declaration."""
    return f'inline constexpr char {symbol}[] = "{wire}";'


class MetricConstantExtraction(unittest.TestCase):
    """L1-metrics: instrument names / label keys / label values are read from
    the right namespace, and comments never seed the set."""

    def _run(self, header_text):
        d = Path(tempfile.mkdtemp())
        try:
            _write(d / "src" / "xrpld" / "telemetry" / "MetricNames.h", header_text)
            return chk.metric_constants(d)
        finally:
            shutil.rmtree(d)

    def test_splits_by_namespace(self):
        names, keys, vals = self._run(
            _metric_header(
                _mc("dnsResolveTotal", "dns_resolve_total"),
                _mc("outcome", "outcome"),
                _mc("resolved", "resolved"),
            )
        )
        self.assertEqual(names, {"dns_resolve_total"})
        self.assertEqual(keys, {"outcome"})
        self.assertEqual(vals, {"resolved"})

    def test_nested_lval_namespace_included(self):
        # `namespace lval { namespace dns_resolve { ... } }` — the inner block is
        # inside the outer's brace-matched span, so its values must be collected.
        _, _, vals = self._run(
            _metric_header(
                lval_body="namespace dns_resolve {\n"
                + _mc("resolved", "resolved")
                + "\n}\n"
            )
        )
        self.assertEqual(vals, {"resolved"})

    def test_commented_constant_not_collected(self):
        names, _, _ = self._run(
            _metric_header(
                "// " + _mc("ghost", "ghost_total") + "\n" + _mc("real", "real_total")
            )
        )
        self.assertEqual(names, {"real_total"})

    def test_block_commented_constant_not_collected(self):
        names, _, _ = self._run(
            _metric_header("/* " + _mc("ghost", "ghost_total") + " */\n")
        )
        self.assertEqual(names, set())

    def test_no_header_empty_sets(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "src").mkdir()
            self.assertEqual(chk.metric_constants(d), (set(), set(), set()))
        finally:
            shutil.rmtree(d)

    def test_namespaced_constant_ignores_its_flat_prefix(self):
        # A `kLabel`-prefixed identifier written inside `namespace metric` must
        # be an instrument name only. The flat prefix pass exists for headers
        # that have no such namespace, so it must not also claim this one:
        # letting it through would make an instrument name a valid label key,
        # and Rule D would then accept `label_peer_total` as a label.
        names, keys, vals = self._run(
            _metric_header(
                metric_body=_mc("kLabelPeerTotal", "label_peer_total"),
                label_body=_mc("outcome", "outcome"),
            )
        )
        self.assertEqual(names, {"label_peer_total"})
        self.assertEqual(keys, {"outcome"})
        self.assertEqual(vals, set())

    def test_flat_prefix_still_read_when_namespaces_absent(self):
        # The counterpart to the test above: with no `namespace metric`/`label`
        # block to excise, the flat pass must still classify by prefix.
        names, keys, vals = self._run(
            "#pragma once\n"
            "namespace xrpl::telemetry {\n"
            + _mc("kLabelOutcome", "outcome")
            + "\n"
            + _mc("kResultResolved", "resolved")
            + "\n}\n"
        )
        self.assertEqual(names, set())
        self.assertEqual(keys, {"outcome"})
        self.assertEqual(vals, {"resolved"})


class RuleIMetricLiterals(unittest.TestCase):
    """Rule I: literal instrument names / label keys at a metric emit site are
    flagged, but only inside a metric FAMILY that already has constants."""

    def _run(self, rel_path, source, header=None):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricNames.h",
                (
                    header
                    if header is not None
                    else _metric_header(
                        _mc("syncState", "sync_state"), _mc("outcome", "outcome")
                    )
                ),
            )
            _write(d / rel_path, source)
            report = chk.Report()
            chk.run_rule_i_metric_literals(d, report)
            return (
                sorted(v[2] for v in report.violations),
                sorted(w[2] for w in report.warnings),
            )
        finally:
            shutil.rmtree(d)

    # ----- positive: a literal in a converted family must FAIL -----
    def test_literal_macro_name_flagged(self):
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC(app, "sync_acquire_total", "d");\n',
        )
        self.assertEqual(v, ['name "sync_acquire_total" (COUNTER_INC)'])

    def test_literal_factory_name_flagged(self):
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'meter_->CreateInt64ObservableGauge("sync_thing", "d");\n',
        )
        self.assertEqual(v, ['name "sync_thing" (CreateInt64ObservableGauge)'])

    def test_literal_label_key_flagged(self):
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC_LABELED(app, metric::syncState, "d", '
            '{{"outcome", std::string(x)}});\n',
        )
        self.assertEqual(v, ['label "outcome" (COUNTER_INC_LABELED)'])

    def test_multiline_call_flagged(self):
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC(\n    app,\n    "sync_x_total",\n    "d");\n',
        )
        self.assertEqual(v, ['name "sync_x_total" (COUNTER_INC)'])

    # ----- negative: things Rule I must NOT flag -----
    def test_constant_name_accepted(self):
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC(app, metric::syncState, "d");\n',
        )
        self.assertEqual(v, [])

    def test_label_value_exempt(self):
        # The VALUE after a constant key is runtime data and must not be flagged.
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC_LABELED(app, metric::syncState, "d", '
            '{{label::outcome, std::string("resolved")}});\n',
        )
        self.assertEqual(v, [])

    def test_description_exempt(self):
        # arg2 is prose, not naming surface.
        v, _ = self._run(
            "src/xrpld/Foo.cpp",
            'XRPL_METRIC_COUNTER_INC(app, metric::syncState, "Some description");\n',
        )
        self.assertEqual(v, [])

    def test_test_path_exempt(self):
        v, _ = self._run(
            "src/tests/libxrpl/telemetry/MetricMacros.cpp",
            'XRPL_METRIC_COUNTER_INC(app, "sync_lit_total", "d");\n',
        )
        self.assertEqual(v, [])

    def test_metricnames_header_exempt(self):
        v, _ = self._run(
            "src/xrpld/telemetry/OtherMetricNames.h",
            'XRPL_METRIC_COUNTER_INC(app, "sync_lit_total", "d");\n',
        )
        self.assertEqual(v, [])

    def test_macro_definition_header_exempt(self):
        v, _ = self._run(
            "src/xrpld/telemetry/MetricMacros.h",
            'XRPL_METRIC_COUNTER_INC(app, "sync_lit_total", "d");\n',
        )
        self.assertEqual(v, [])

    # ----- the ratchet: an unconverted family warns (L) instead of failing -----
    def test_unconverted_family_warns_not_fails(self):
        v, w = self._run(
            "src/xrpld/Foo.cpp",
            'meter_->CreateDoubleObservableGauge("txq_metrics", "d");\n',
        )
        self.assertEqual(v, [])
        self.assertEqual(w, ['name "txq_metrics" (CreateDoubleObservableGauge)'])

    def test_skip_when_no_metric_header(self):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "Foo.cpp",
                'XRPL_METRIC_COUNTER_INC(app, "x_total", "d");\n',
            )
            report = chk.Report()
            chk.run_rule_i_metric_literals(d, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("I" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


class RuleJMetricSuffixes(unittest.TestCase):
    """Rule J: instrument names follow the kind-appropriate suffix rules, with
    the KIND read from the emit site rather than guessed from the name."""

    def _run(self, metric_body, emit_source=""):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricNames.h",
                _metric_header(metric_body),
            )
            if emit_source:
                _write(d / "src" / "xrpld" / "Foo.cpp", emit_source)
            report = chk.Report()
            chk.run_rule_j_metric_suffixes(d, report)
            return sorted((v[2], v[3]) for v in report.violations)
        finally:
            shutil.rmtree(d)

    # ----- positive -----
    def test_counter_without_total_flagged(self):
        self.assertEqual(
            self._run(
                _mc("dnsResolve", "dns_resolve"),
                'XRPL_METRIC_COUNTER_INC(app, metric::dnsResolve, "d");\n',
            ),
            [("dns_resolve", "counter must end in _total")],
        )

    def test_histogram_without_unit_flagged(self):
        self.assertEqual(
            self._run(
                _mc("dialLatency", "overlay_dial_latency"),
                'XRPL_METRIC_HISTOGRAM_RECORD(app, metric::dialLatency, "d", v);\n',
            ),
            [("overlay_dial_latency", "histogram needs _us/_ms/_seconds suffix")],
        )

    def test_gauge_with_total_flagged(self):
        self.assertEqual(
            self._run(
                _mc("syncTotal", "sync_total"),
                'meter_->CreateInt64ObservableGauge(metric::syncTotal, "d");\n',
            ),
            [("sync_total", "_total is reserved for counters")],
        )

    def test_prefixed_name_flagged(self):
        self.assertEqual(
            self._run(_mc("prefixed", "xrpld_sync_state")),
            [("xrpld_sync_state", "drop the prefix; the exporter adds it")],
        )

    def test_non_snake_case_flagged(self):
        self.assertEqual(
            self._run(_mc("camel", "syncState")),
            [("syncState", "must be lower_snake_case")],
        )

    # ----- negative -----
    def test_conforming_counter_accepted(self):
        self.assertEqual(
            self._run(
                _mc("dnsResolveTotal", "dns_resolve_total"),
                'XRPL_METRIC_COUNTER_INC(app, metric::dnsResolveTotal, "d");\n',
            ),
            [],
        )

    def test_conforming_histogram_accepted(self):
        self.assertEqual(
            self._run(
                _mc("dialMs", "overlay_dial_latency_ms"),
                'XRPL_METRIC_HISTOGRAM_RECORD(app, metric::dialMs, "d", v);\n',
            ),
            [],
        )

    def test_gauge_with_unit_bearing_label_values_is_not_flagged(self):
        # The regression this rule's kind-awareness exists for: a multi-series
        # GAUGE whose units live in its label VALUES (nodestore_state
        # observing write_mean_us) must not be read as a mis-suffixed duration.
        self.assertEqual(
            self._run(
                _mc("nodestoreState", "nodestore_state"),
                'meter_->CreateInt64ObservableGauge(metric::nodestoreState, "d");\n',
            ),
            [],
        )

    def test_unknown_kind_only_shape_checked(self):
        # With no emit site found, the kind is unknown, so only the
        # shape-independent rules apply -- a bare gauge-ish name is fine.
        self.assertEqual(self._run(_mc("syncState", "sync_state")), [])

    def test_one_name_created_as_two_kinds_is_flagged(self):
        # One wire name built through two different factories exports two
        # instruments under a single name, and no suffix can satisfy both. The
        # kind map therefore records a SET per name: keeping only the last kind
        # visited silently hid this, because whichever emit site the walk
        # reached last decided the verdict.
        violations = self._run(
            _mc("dualKind", "dual_kind_total"),
            'meter_->CreateUInt64Counter(metric::dualKind, "d");\n'
            'meter_->CreateInt64ObservableGauge(metric::dualKind, "d");\n',
        )
        self.assertEqual(len(violations), 1, violations)
        # The message names both kinds, so the reader sees the conflict rather
        # than a suffix complaint that would contradict one of the two sites.
        self.assertIn("counter", violations[0][-1])
        self.assertIn("gauge", violations[0][-1])

    def test_two_kinds_where_the_last_one_alone_looks_clean(self):
        # The sharper case for the same bug. Here the LAST emit site visited is a
        # histogram and the name carries a duration suffix, so under last-wins
        # semantics Rule J saw a well-formed histogram and reported nothing at
        # all -- the conflict was not merely mislabelled, it was invisible. With
        # a set per name the mismatch surfaces regardless of walk order.
        violations = self._run(
            _mc("dualShape", "dual_shape_us"),
            'meter_->CreateInt64ObservableGauge(metric::dualShape, "d");\n'
            'meter_->CreateUInt64Histogram(metric::dualShape, "d");\n',
        )
        self.assertEqual(len(violations), 1, violations)
        self.assertIn("gauge", violations[0][-1])
        self.assertIn("histogram", violations[0][-1])

    def test_skip_when_no_header(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "src").mkdir()
            report = chk.Report()
            chk.run_rule_j_metric_suffixes(d, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("J" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


class RuleKExpectedMetrics(unittest.TestCase):
    """Rule K: a name in expected_metrics.json inside a converted family must
    resolve to a *MetricNames.h constant."""

    def _run(self, json_text, metric_body):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricNames.h",
                _metric_header(metric_body),
            )
            _write(
                d / "docker" / "telemetry" / "workload" / "expected_metrics.json",
                json_text,
            )
            report = chk.Report()
            chk.run_rule_k_expected_metrics(d, report)
            return sorted(v[2] for v in report.violations), report.skips
        finally:
            shutil.rmtree(d)

    def test_stale_name_in_converted_family_flagged(self):
        # `sync_` is owned (sync_state is declared), so a sibling that resolves
        # to nothing is the rename-drift this rule exists to catch.
        v, _ = self._run(
            '{"metrics": [{"metric": "sync_stale_total"}]}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, ["sync_stale_total"])

    def test_declared_name_accepted(self):
        v, _ = self._run(
            '{"metrics": [{"metric": "sync_state"}]}', _mc("syncState", "sync_state")
        )
        self.assertEqual(v, [])

    def test_unowned_family_not_flagged(self):
        # `txq_` has no constants, so its entries are out of scope rather than
        # a failure -- the ratchet again.
        v, _ = self._run(
            '{"metrics": [{"metric": "txq_metrics"}]}', _mc("syncState", "sync_state")
        )
        self.assertEqual(v, [])

    def test_name_key_also_read(self):
        v, _ = self._run(
            '{"metrics": [{"name": "sync_stale_total"}]}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, ["sync_stale_total"])

    def test_nested_structure_walked(self):
        v, _ = self._run(
            '{"groups": {"a": {"items": [{"metric": "sync_stale_total"}]}}}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, ["sync_stale_total"])

    def test_promql_selector_stripped(self):
        # Entries are written as an operator would query them; the selector must
        # be stripped before the constant lookup or every entry would fail.
        v, _ = self._run(
            '{"g": {"metrics": ["sync_state{metric=\\"ledgers_behind\\"}"]}}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, [])

    def test_histogram_suffix_stripped(self):
        # _bucket/_count/_sum are appended by the exporter, never named in code.
        v, _ = self._run(
            '{"g": {"metrics": ["overlay_dial_latency_ms_bucket"]}}',
            _mc("dialMs", "overlay_dial_latency_ms"),
        )
        self.assertEqual(v, [])

    def test_statsd_group_skipped(self):
        # beast::insight names come from formatName(), not the OTel API, so a
        # statsd group must not be held to a *MetricNames.h constant.
        v, _ = self._run(
            '{"statsd_gauges": {"metrics": ["sync_not_a_constant"]}}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, [])

    def test_spanmetrics_group_skipped(self):
        v, _ = self._run(
            '{"spanmetrics": {"metrics": ["sync_span_calls_total"]}}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, [])

    def test_description_prose_not_treated_as_metric(self):
        v, _ = self._run(
            '{"g": {"description": "sync_ stuff about metrics", "metrics": []}}',
            _mc("syncState", "sync_state"),
        )
        self.assertEqual(v, [])

    def test_malformed_json_reported(self):
        v, _ = self._run("{not json", _mc("syncState", "sync_state"))
        self.assertEqual(len(v), 1)

    def test_skip_when_file_absent(self):
        d = Path(tempfile.mkdtemp())
        try:
            _write(
                d / "src" / "xrpld" / "telemetry" / "MetricNames.h",
                _metric_header(_mc("syncState", "sync_state")),
            )
            report = chk.Report()
            chk.run_rule_k_expected_metrics(d, report)
            self.assertEqual(report.violations, [])
            self.assertTrue(any("K" in s for s in report.skips))
        finally:
            shutil.rmtree(d)


class InstrumentKindClassification(unittest.TestCase):
    """classify_instrument_kind: `UpDown`/`Observable` are checked before the
    bare `Counter` substring they both contain."""

    def test_macro_kinds(self):
        for kind, want in (
            ("COUNTER_INC", "counter"),
            ("COUNTER_ADD_LABELED", "counter"),
            ("HISTOGRAM_RECORD", "histogram"),
            ("UPDOWN_ADD", "updown"),
            ("OBSERVABLE_GAUGE_REGISTER", "gauge"),
        ):
            self.assertEqual(chk.classify_instrument_kind(kind), want, kind)

    def test_factory_kinds(self):
        for kind, want in (
            ("CreateUInt64Counter", "counter"),
            ("CreateDoubleHistogram", "histogram"),
            ("CreateInt64UpDownCounter", "updown"),
            ("CreateInt64ObservableGauge", "gauge"),
            ("CreateInt64ObservableCounter", "counter"),
            ("CreateInt64ObservableUpDownCounter", "updown"),
        ):
            self.assertEqual(chk.classify_instrument_kind(kind), want, kind)


class MetricPrefixFamilies(unittest.TestCase):
    def test_first_segment_is_the_family(self):
        self.assertEqual(
            chk.metric_prefixes({"sync_state", "jobq_saturation", "unl_quorum"}),
            {"sync_", "jobq_", "unl_"},
        )

    def test_name_without_underscore_has_no_family(self):
        self.assertEqual(chk.metric_prefixes({"uptime"}), set())


if __name__ == "__main__":
    unittest.main(verbosity=2)
