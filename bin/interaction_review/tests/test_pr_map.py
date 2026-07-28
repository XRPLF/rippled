"""Mapping a diff onto graph nodes.

The graph fixture is hand-written and small so each expected match is checkable
by eye; a separate test runs the same logic against the real generated graph.
"""

import json
from pathlib import Path

import pytest

import build_graph as bg
import pr_map
from diffparse import parse_unified_diff

HERE = Path(__file__).resolve().parent
TOOL_DIR = HERE.parent
REPO_ROOT = HERE.parents[2]

FORK_FILE = "src/libxrpl/tx/Transactor.cpp"
IMPL_FILE = "src/libxrpl/tx/transactors/payment/Payment.cpp"
MACRO_FILE = "include/xrpl/protocol/detail/transactions.macro"

# Resources are listed in *descending* line order on purpose. The real graph
# orders nodes by name, never by line, and an ascending fixture is exactly what
# hid the _merge_ranges bug that made in-span edits report as off-span.
GRAPH = {
    "schema": 1,
    "vocabulary": {
        "common_fields": ["sfDelegate", "sfSponsor", "sfSigners"],
        "flags": ["tfInnerBatchTxn"],
        "amendment_globals": [
            "featureBatchV1_1",
            "fixAMMv1_3",
            "featureConfidentialTransfer",
        ],
    },
    "features": [
        {
            "id": "feature:transactor:Payment",
            "kind": "transactor",
            "name": "Payment",
            "locations": [
                {
                    "file": MACRO_FILE,
                    "start_line": 27,
                    "end_line": 41,
                    "role": "macro",
                },
                {"file": IMPL_FILE, "start_line": 1, "end_line": 800, "role": "impl"},
            ],
        },
        {
            "id": "feature:amendment:BatchV1_1",
            "kind": "amendment",
            "name": "BatchV1_1",
            "locations": [
                {
                    "file": "include/xrpl/protocol/detail/features.macro",
                    "start_line": 19,
                    "end_line": 19,
                    "role": "macro",
                }
            ],
        },
    ],
    "resources": [
        {
            "id": "resource:fork:checkSign",
            "kind": "fork",
            "name": "checkSign",
            "signal": "high",
            "lever_fields": ["sfSigners"],
            "lever_flags": [],
            "amendment_gates": [],
            "locations": [
                {
                    "file": FORK_FILE,
                    "start_line": 300,
                    "end_line": 350,
                    "role": "definition",
                },
                {
                    "file": FORK_FILE,
                    "start_line": 360,
                    "end_line": 370,
                    "role": "definition",
                },
            ],
        },
        {
            "id": "resource:fork:getFeePayer",
            "kind": "fork",
            "name": "getFeePayer",
            "signal": "high",
            "lever_fields": ["sfDelegate", "sfSponsor"],
            "lever_flags": [],
            "amendment_gates": ["BatchV1_1"],
            "locations": [
                {
                    "file": FORK_FILE,
                    "start_line": 100,
                    "end_line": 200,
                    "role": "definition",
                }
            ],
        },
    ],
    "edges": [],
}


def _report(diff_text):
    return pr_map.build_report(
        GRAPH, parse_unified_diff(diff_text), base="base0", head="head0"
    )


def _entry(report, node_id):
    return next((t for t in report["touched"] if t["id"] == node_id), None)


def test_change_inside_definition_span_is_a_span_match():
    report = _report(f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +150,1 @@\n+x\n")
    entry = _entry(report, "resource:fork:getFeePayer")
    assert entry["match"] == "span"
    assert entry["evidence"][0]["lines"] == [[150, 150]]
    # The other fork in the same file was not edited, so it is not touched.
    assert _entry(report, "resource:fork:checkSign") is None


def test_change_in_impl_file_is_only_a_file_match():
    """An impl span is the whole file, so hitting it says 'somewhere in
    Payment.cpp' and nothing more precise."""
    report = _report(f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+x\n")
    entry = _entry(report, "feature:transactor:Payment")
    assert entry["match"] == "file"
    assert [e["role"] for e in entry["evidence"]] == ["impl"]


def test_macro_row_edit_upgrades_transactor_to_span():
    report = _report(f"--- a/{MACRO_FILE}\n+++ b/{MACRO_FILE}\n@@ -30,0 +30,1 @@\n+x\n")
    assert _entry(report, "feature:transactor:Payment")["match"] == "span"


def test_both_impl_and_macro_hits_report_span():
    diff = (
        f"--- a/{MACRO_FILE}\n+++ b/{MACRO_FILE}\n@@ -30,0 +30,1 @@\n+x\n"
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+y\n"
    )
    entry = _entry(_report(diff), "feature:transactor:Payment")
    assert entry["match"] == "span"
    assert {e["role"] for e in entry["evidence"]} == {"macro", "impl"}


def test_off_span_change_is_not_attributed_to_any_node():
    """The point of the off-span bucket: an edit to a free helper in
    Transactor.cpp must not be charged to all the forks that file contains."""
    report = _report(f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -60,0 +61,1 @@\n+x\n")
    assert report["touched"] == []
    assert report["off_span_changes"] == [
        {
            "file": FORK_FILE,
            "lines": [[61, 61]],
            "hosts": ["resource:fork:checkSign", "resource:fork:getFeePayer"],
        }
    ]


def test_span_and_off_span_in_one_file_are_both_reported():
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n"
        "@@ -60,0 +61,1 @@\n+outside\n"
        "@@ -150,0 +151,1 @@\n+inside\n"
    )
    report = _report(diff)
    assert _entry(report, "resource:fork:getFeePayer")["match"] == "span"
    assert report["off_span_changes"][0]["lines"] == [[61, 61]]


def test_second_overload_span_matches_the_merged_fork():
    report = _report(f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -364,0 +365,1 @@\n+x\n")
    entry = _entry(report, "resource:fork:checkSign")
    assert entry["match"] == "span"
    assert entry["evidence"][0]["span"] == [360, 370]


def test_known_and_new_levers_are_separated():
    """A lever the graph already knows for this fork is confirmation; one it
    does not know means the diff is changing the fork's edge set."""
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +150,2 @@\n"
        "+    if (tx.isFieldPresent(sfDelegate)) return 0;\n"
        "+    if (view.rules().enabled(featureConfidentialTransfer)) return 1;\n"
    )
    entry = _entry(_report(diff), "resource:fork:getFeePayer")
    assert entry["known_levers"] == ["sfDelegate"]
    assert entry["new_levers"] == ["featureConfidentialTransfer"]


@pytest.mark.parametrize(
    "gate,token",
    [("BatchV1_1", "featureBatchV1_1"), ("AMMv1_3", "fixAMMv1_3")],
)
def test_gate_is_known_under_both_spellings(gate, token):
    """Gates are stored bare ('BatchV1_1') but written 'featureX' or 'fixX'.
    The `fix` spelling is the more common one in rippled and was untested —
    dropping it made every fix-gated fork report a false new lever."""
    graph = json.loads(json.dumps(GRAPH))
    fork = next(r for r in graph["resources"] if r["name"] == "getFeePayer")
    fork["amendment_gates"] = [gate]
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +150,1 @@\n"
        f"+    if (view.rules().enabled({token})) return 2;\n"
    )
    report = pr_map.build_report(graph, parse_unified_diff(diff), base="b", head="h")
    entry = next(t for t in report["touched"] if t["name"] == "getFeePayer")
    assert entry["known_levers"] == [token]
    assert entry["new_levers"] == []


def test_in_span_edit_is_never_also_reported_off_span():
    """Regression for _merge_ranges: with node spans arriving in node order the
    coverage set lost every span sorting before the first, so in-span lines were
    simultaneously attributed and reported as unattributed."""
    report = _report(f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +151,1 @@\n+x\n")
    assert _entry(report, "resource:fork:getFeePayer")["match"] == "span"
    assert report["off_span_changes"] == []


def test_hits_are_clipped_to_the_node_span():
    """One contiguous hunk can straddle two definitions and the gap between
    them. Lines outside a span are not evidence about that node — and must not
    supply its lever tokens."""
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -190,0 +190,120 @@\n"
        + "".join(f"+  line{i}\n" for i in range(190, 250))
        + "".join(
            (
                "+    if (view.rules().enabled(featureConfidentialTransfer)) return 1;\n"
                if i == 250
                else f"+  line{i}\n"
            )
            for i in range(250, 310)
        )
    )
    report = _report(diff)
    fee = _entry(report, "resource:fork:getFeePayer")
    # getFeePayer spans 100-200, so only 190-200 of the hunk belongs to it.
    assert fee["evidence"][0]["lines"] == [[190, 200]]
    # The gate token sits at line 250, inside neither span.
    assert fee["new_levers"] == []


def test_deleted_node_file_is_surfaced():
    """Deleting Transactor.cpp removes every fork; reporting nothing would be
    the worst possible failure."""
    diff = f"--- a/{FORK_FILE}\n+++ /dev/null\n@@ -1,400 +0,0 @@\n-x\n"
    assert _report(diff)["structural_changes"] == [
        {
            "file": FORK_FILE,
            "kind": "deleted",
            "hosted": ["resource:fork:checkSign", "resource:fork:getFeePayer"],
        }
    ]


def test_content_free_change_to_a_node_file_is_surfaced():
    """A 100%-similarity rename or a mode change has no hunks at all. Such a
    file used to fall out of every bucket and be reported nowhere."""
    diff = (
        f"diff --git a/old/Payment.cpp b/{IMPL_FILE}\n"
        "similarity index 100%\n"
        f"rename from old/Payment.cpp\nrename to {IMPL_FILE}\n"
    )
    assert _report(diff)["structural_changes"] == [
        {
            "file": IMPL_FILE,
            "kind": "no_line_changes",
            "hosted": ["feature:transactor:Payment"],
        }
    ]


def test_every_changed_file_is_accounted_for():
    """The output buckets must partition the changed-file set. A file present in
    none of them is a silent drop — the failure this module exists to avoid."""
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +151,1 @@\n+in-span\n"
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -60,0 +61,1 @@\n+off-span\n"
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+impl\n"
        "--- a/docs/README.md\n+++ b/docs/README.md\n@@ -1,0 +2,1 @@\n+doc\n"
        f"diff --git a/old.cpp b/{MACRO_FILE}\nsimilarity index 100%\n"
        f"rename from old.cpp\nrename to {MACRO_FILE}\n"
        "--- a/gone.cpp\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-a\n-b\n"
    )
    diffs = parse_unified_diff(diff)
    report = pr_map.build_report(GRAPH, diffs, base="b", head="h")
    accounted = set(report["unmapped_files"])
    accounted |= {s["file"] for s in report["structural_changes"]}
    accounted |= {o["file"] for o in report["off_span_changes"]}
    accounted |= {ev["file"] for t in report["touched"] for ev in t["evidence"]}
    assert accounted == set(diffs), set(diffs) - accounted
    assert report["summary"]["changed_files"] == len(diffs)


def test_file_match_upgrades_to_span_when_impl_seen_first():
    """resolve_touched iterates files in sorted order, so a src/ impl hit can
    arrive before an include/ macro hit; the upgrade must still happen."""
    graph = json.loads(json.dumps(GRAPH))
    payment = next(f for f in graph["features"] if f["name"] == "Payment")
    # Put the precise span in a file that sorts *after* the impl file.
    payment["locations"] = [
        {"file": IMPL_FILE, "start_line": 1, "end_line": 800, "role": "impl"},
        {"file": "zzz.macro", "start_line": 5, "end_line": 5, "role": "macro"},
    ]
    diff = (
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+x\n"
        "--- a/zzz.macro\n+++ b/zzz.macro\n@@ -5,0 +5,1 @@\n+y\n"
    )
    report = pr_map.build_report(graph, parse_unified_diff(diff), base="b", head="h")
    assert (
        next(t for t in report["touched"] if t["name"] == "Payment")["match"] == "span"
    )


def test_lever_tokens_are_validated_against_the_vocabulary():
    """`fixEnabled` and `sfBalance` are shaped like levers but are a local and a
    non-common field; neither can ever be one."""
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +150,2 @@\n"
        "+    bool const fixEnabled = view.rules().enabled(fixAMMv1_3);\n"
        "+    auto const bal = sle->at(sfBalance);\n"
    )
    entry = _entry(_report(diff), "resource:fork:getFeePayer")
    assert "fixEnabled" not in entry["new_levers"]
    assert "sfBalance" not in entry["new_levers"]
    assert entry["new_levers"] == ["fixAMMv1_3"]


def test_levers_collected_on_non_fork_nodes_too():
    """Most amendment gating lives in transactor implementations; a diff adding
    a gate to Payment.cpp is exactly the coupling this tool looks for."""
    diff = (
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n"
        "+    if (view.rules().enabled(featureConfidentialTransfer)) return tecX;\n"
    )
    entry = _entry(_report(diff), "feature:transactor:Payment")
    assert entry["new_levers"] == ["featureConfidentialTransfer"]


@pytest.mark.parametrize(
    "text,expected",
    [
        ("tx[sfAccount]", {"sfAccount"}),
        ("isFlag(tfInnerBatchTxn)", {"tfInnerBatchTxn"}),
        ("enabled(fixAMMv1_3)", {"fixAMMv1_3"}),
        ("auto sfield = 1;", set()),  # lowercase after prefix -> not a lever
        ("fixup(features, tfoo)", set()),
        ("a.b(sfAmount) + c[sfSequence]", {"sfAmount", "sfSequence"}),
    ],
)
def test_lever_shape_recognition(text, expected):
    assert pr_map._lever_shaped(text) == expected


def test_vocabulary_filters_lever_shaped_non_levers():
    vocab = {"sfDelegate", "fixAMMv1_3"}
    text = "fixEnabled = enabled(fixAMMv1_3); x = tx[sfDelegate]; y = sfBalance;"
    assert pr_map._lever_tokens(text, vocab) == {"sfDelegate", "fixAMMv1_3"}
    # With no vocabulary (an older graph) shape is all we have.
    assert "fixEnabled" in pr_map._lever_tokens(text, None)


def test_unmapped_files_are_listed_not_dropped():
    report = _report(
        "--- a/docs/README.md\n+++ b/docs/README.md\n@@ -1,0 +2,1 @@\n+x\n"
    )
    assert report["unmapped_files"] == ["docs/README.md"]
    assert report["touched"] == []


def test_span_matches_sort_before_file_matches():
    diff = (
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+x\n"
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +151,1 @@\n+y\n"
    )
    assert [t["match"] for t in _report(diff)["touched"]] == ["span", "file"]


def test_report_validates_against_schema():
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +151,1 @@\n+x\n"
        f"--- a/{IMPL_FILE}\n+++ b/{IMPL_FILE}\n@@ -40,0 +41,1 @@\n+y\n"
        "--- a/docs/README.md\n+++ b/docs/README.md\n@@ -1,0 +2,1 @@\n+z\n"
    )
    pr_map.validate_report(_report(diff), TOOL_DIR / "touched.schema.json")


def test_summary_counts_are_consistent():
    diff = (
        f"--- a/{FORK_FILE}\n+++ b/{FORK_FILE}\n@@ -150,0 +151,1 @@\n+x\n"
        "--- a/docs/README.md\n+++ b/docs/README.md\n@@ -1,0 +2,1 @@\n+z\n"
    )
    report = _report(diff)
    summary = report["summary"]
    assert summary["changed_files"] == 2
    assert summary["unmapped_files"] == len(report["unmapped_files"]) == 1
    assert summary["structural_files"] == len(report["structural_changes"])
    assert summary["touched_nodes"] == len(report["touched"]) == 1
    assert sum(summary["by_match"].values()) == summary["touched_nodes"]
    assert sum(summary["by_kind"].values()) == summary["touched_nodes"]


def test_load_graph_rejects_non_graph(tmp_path):
    bad = tmp_path / "x.json"
    bad.write_text('{"nope": 1}')
    with pytest.raises(ValueError, match="not a graph document"):
        pr_map.load_graph(bad)


def test_load_graph_rejects_wrong_schema_version(tmp_path):
    stale = tmp_path / "graph.json"
    stale.write_text(json.dumps({**GRAPH, "schema": 99}))
    with pytest.raises(ValueError, match="schema version"):
        pr_map.load_graph(stale)


def test_load_graph_validates_against_the_schema(tmp_path):
    broken = json.loads(json.dumps(GRAPH))
    broken["features"][0]["locations"] = []  # violates minItems
    path = tmp_path / "graph.json"
    path.write_text(json.dumps(broken))
    with pytest.raises(ValueError, match="failed graph schema validation"):
        pr_map.load_graph(path)


# --- against the real generated graph ---------------------------------------


@pytest.fixture(scope="module")
def real_graph(tmp_path_factory, libclang_dylib, build_dir):
    out = tmp_path_factory.mktemp("pr_map_graph")
    assert bg.main(["--build-dir", str(build_dir), "--out", str(out)]) == 0
    return json.loads((out / "graph.json").read_text())


def test_real_graph_maps_a_fork_edit(real_graph):
    """A change inside the real getFeePayer body must resolve to that fork and
    nothing else in Transactor.cpp."""
    fork = next(
        r for r in real_graph["resources"] if r["id"] == "resource:fork:getFeePayer"
    )
    loc = fork["locations"][0]
    line = loc["start_line"] + 1
    diff = (
        f"--- a/{loc['file']}\n+++ b/{loc['file']}\n@@ -{line - 1},0 +{line},1 @@\n"
        "+    if (view.rules().enabled(featureConfidentialTransfer)) return 1;\n"
    )
    report = pr_map.build_report(
        real_graph, parse_unified_diff(diff), base="b", head="h"
    )
    forks = [t for t in report["touched"] if t["kind"] == "fork"]
    assert [t["name"] for t in forks] == ["getFeePayer"]
    assert forks[0]["new_levers"] == ["featureConfidentialTransfer"]
    pr_map.validate_report(report, TOOL_DIR / "touched.schema.json")


def test_real_graph_maps_a_transactor_impl_edit(real_graph):
    impl = next(
        loc["file"]
        for node in real_graph["features"]
        if node["name"] == "AMMWithdraw"
        for loc in node["locations"]
        if loc["role"] == "impl" and loc["file"].endswith(".cpp")
    )
    diff = f"--- a/{impl}\n+++ b/{impl}\n@@ -20,0 +21,1 @@\n+    // tweak\n"
    report = pr_map.build_report(
        real_graph, parse_unified_diff(diff), base="b", head="h"
    )
    assert "feature:transactor:AMMWithdraw" in {t["id"] for t in report["touched"]}
