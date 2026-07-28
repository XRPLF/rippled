"""End-to-end build against the checkout, guardrails, and schema validation."""

import json
import re
from pathlib import Path

import pytest

import build_graph as bg
from fork_extractor import ForkResult
from graph import (
    EDGE_CONSUMER,
    FEATURE_TRANSACTOR,
    RESOURCE_FORK,
    Edge,
    FeatureNode,
    GraphBuilder,
    feature_id,
    resource_id,
)

REPO_ROOT = Path(__file__).resolve().parents[3]


def _expected_transactors() -> int:
    text = (REPO_ROOT / bg.TRANSACTIONS_MACRO).read_text()
    return len(re.findall(r"^TRANSACTION\(", text, re.M))


def _expected_amendments() -> int:
    n = 0
    for line in (REPO_ROOT / bg.FEATURES_MACRO).read_text().splitlines():
        s = line.lstrip()
        if not s.startswith("//") and re.match(r"XRPL_(FEATURE|FIX)\s*\(", s):
            n += 1
    return n


def _expected_invariants() -> int:
    block = (REPO_ROOT / bg.PRIVILEGE_HEADER).read_text()
    block = block.split("enum Privilege", 1)[1].split("};", 1)[0]
    names = re.findall(r"(\w+)\s*=\s*0x[0-9A-Fa-f]+", block)
    return len([n for n in names if n != "NoPriv"])


@pytest.fixture(scope="module")
def outputs(tmp_path_factory, libclang_dylib, build_dir):
    out = tmp_path_factory.mktemp("graph_out")
    rc = bg.main(["--build-dir", str(build_dir), "--out", str(out)])
    assert rc == 0
    graph = json.loads((out / "graph.json").read_text())
    interactions = json.loads((out / "interactions.json").read_text())
    return graph, interactions


def test_counts(outputs):
    graph, _ = outputs
    kinds = [f["kind"] for f in graph["features"]]
    assert kinds.count("transactor") == _expected_transactors()
    assert kinds.count("amendment") == _expected_amendments()
    invariants = [r for r in graph["resources"] if r["kind"] == "invariant"]
    assert len(invariants) == _expected_invariants()


def test_batch_node(outputs):
    graph, _ = outputs
    batch = next(f for f in graph["features"] if f["name"] == "Batch")
    assert batch["wrapper"] is True
    assert batch["delegable"] is False
    assert batch["amendment"] == "BatchV1_1"


def test_acceptance_interaction(outputs):
    _, interactions = outputs
    acc = [
        i
        for i in interactions["interactions"]
        if i["resource"] == "getFeePayer"
        and set(i["features"]) == {"Batch", "PermissionDelegationV1_1"}
    ]
    assert len(acc) == 1
    assert acc[0]["kind"] == "mediator×mediator"


def test_no_consumer_consumer_on_forks(outputs):
    _, interactions = outputs
    for i in interactions["interactions"]:
        if i["resource_kind"] == "fork":
            assert i["kind"] != "consumer×consumer"


# --- guardrails -------------------------------------------------------------


def _macro_only_builder():
    from common_fields import parse_common_fields
    from macro_extractor import extract_macros

    common = parse_common_fields(REPO_ROOT / bg.TXFORMATS_CPP)
    builder = GraphBuilder()
    globals_map = extract_macros(
        builder,
        REPO_ROOT,
        REPO_ROOT / bg.TRANSACTIONS_MACRO,
        REPO_ROOT / bg.FEATURES_MACRO,
        REPO_ROOT / bg.SFIELDS_MACRO,
        common,
    )
    return builder, globals_map, common


def test_recall_guardrail_missing_field(build_dir, libclang_dylib):
    from fork_extractor import extract_forks

    builder, globals_map, common = _macro_only_builder()
    forks = extract_forks(build_dir, common)
    field_table = bg.load_table(bg.HERE / "config" / "field_to_amendment.yml")
    field_table.pop("sfDelegate")
    flag_table = bg.load_table(bg.HERE / "config" / "flag_to_amendment.yml")
    with pytest.raises(ValueError, match="sfDelegate"):
        bg.merge_forks(builder, forks, globals_map, field_table, flag_table)


def test_dangling_mediator_table_value(build_dir, libclang_dylib):
    from fork_extractor import extract_forks

    builder, globals_map, common = _macro_only_builder()
    forks = extract_forks(build_dir, common)
    field_table = bg.load_table(bg.HERE / "config" / "field_to_amendment.yml")
    field_table["sfDelegate"] = "NoSuchAmendment"
    flag_table = bg.load_table(bg.HERE / "config" / "flag_to_amendment.yml")
    with pytest.raises(ValueError, match="dangling"):
        bg.merge_forks(builder, forks, globals_map, field_table, flag_table)


def test_schema_catches_dangling_edge():
    from graph import LOC_MACRO, Location

    builder = GraphBuilder()
    builder.add_feature(
        FeatureNode(
            feature_id(FEATURE_TRANSACTOR, "T"),
            FEATURE_TRANSACTOR,
            "T",
            locations=[Location(bg.TRANSACTIONS_MACRO, 1, 1, LOC_MACRO)],
        )
    )
    # Edge to a resource that was never added.
    builder.add_edge(
        Edge(
            EDGE_CONSUMER,
            feature_id(FEATURE_TRANSACTOR, "T"),
            resource_id(RESOURCE_FORK, "ghost"),
            "base",
        )
    )
    with pytest.raises(ValueError, match="unknown resource"):
        bg.validate_graph(builder, bg.HERE / "graph.schema.json")


def test_unresolved_gate_is_fatal():
    builder, globals_map, _ = _macro_only_builder()
    ghost = ForkResult(name="checkSign", gate_globals={"featureNotAReal"})
    with pytest.raises(ValueError, match="no active amendment"):
        bg.merge_forks(builder, [ghost], globals_map, {}, {})


def test_gate_allowlist_tolerates_unresolved():
    builder, globals_map, _ = _macro_only_builder()
    ghost = ForkResult(name="checkSign", gate_globals={"featureRetiredButReferenced"})
    # Allowlisted -> no raise; the fork resource is created with no gate edge.
    bg.merge_forks(
        builder,
        [ghost],
        globals_map,
        {},
        {},
        gate_allowlist={"featureRetiredButReferenced"},
    )
    res = builder.resources[resource_id(RESOURCE_FORK, "checkSign")]
    assert res.amendment_gates == []


# --- locations (Phase 0) ----------------------------------------------------


def test_every_node_is_locatable(outputs):
    """A node with no location can never be reached by a diff, so an unlocated
    node is a silent recall hole in the PR mapper."""
    graph, _ = outputs
    unlocated = [
        n["id"] for n in graph["features"] + graph["resources"] if not n["locations"]
    ]
    assert unlocated == []


def test_locations_are_repo_relative_and_in_bounds(outputs):
    graph, _ = outputs
    for node in graph["features"] + graph["resources"]:
        for loc in node["locations"]:
            assert not loc["file"].startswith("/"), loc
            path = REPO_ROOT / loc["file"]
            assert path.is_file(), loc
            assert 1 <= loc["start_line"] <= loc["end_line"]
            assert loc["end_line"] <= max(1, len(path.read_text().splitlines()))


def test_transactor_has_macro_row_and_impl(outputs):
    graph, _ = outputs
    payment = next(f for f in graph["features"] if f["name"] == "Payment")
    roles = {loc["role"] for loc in payment["locations"]}
    assert roles == {"macro", "impl"}
    macro = next(loc for loc in payment["locations"] if loc["role"] == "macro")
    assert macro["file"] == bg.TRANSACTIONS_MACRO
    row = (REPO_ROOT / macro["file"]).read_text().splitlines()[macro["start_line"] - 1]
    assert row.startswith("TRANSACTION(") and "Payment" in row
    impls = {loc["file"] for loc in payment["locations"] if loc["role"] == "impl"}
    assert any(f.endswith("payment/Payment.cpp") for f in impls)


def test_aliased_transactors_resolve_to_their_real_file(outputs):
    """The three pseudo-transactions alias to Change and the renamed XChain
    transactors to XChainBridge; neither has a file of its own."""
    graph, _ = outputs
    by_name = {f["name"]: f for f in graph["features"]}
    for name, expected in (
        ("EnableAmendment", "system/Change.cpp"),
        ("SetFee", "system/Change.cpp"),
        ("UNLModify", "system/Change.cpp"),
        ("XChainModifyBridge", "bridge/XChainBridge.cpp"),
        ("XChainAccountCreateCommit", "bridge/XChainBridge.cpp"),
    ):
        impls = {loc["file"] for loc in by_name[name]["locations"]}
        assert any(f.endswith(expected) for f in impls), (name, impls)


def test_merged_fork_overloads_keep_one_span_each(outputs):
    """checkSign has two overloads that merge into one resource; both spans must
    survive, or a diff to one of them maps nowhere."""
    graph, _ = outputs
    fork = next(r for r in graph["resources"] if r["id"] == "resource:fork:checkSign")
    spans = [loc for loc in fork["locations"] if loc["role"] == "definition"]
    assert len(spans) >= 2
    assert all(loc["file"].endswith("tx/Transactor.cpp") for loc in spans)
    # Spans are disjoint: distinct definitions, not one span recorded twice.
    ordered = sorted((loc["start_line"], loc["end_line"]) for loc in spans)
    for (_, prev_end), (next_start, _) in zip(ordered, ordered[1:]):
        assert prev_end < next_start


def test_helper_header_fork_span(outputs):
    graph, _ = outputs
    fork = next(
        r for r in graph["resources"] if r["id"] == "resource:fork:isFeeSponsored"
    )
    assert any("/helpers/" in loc["file"] for loc in fork["locations"])


def test_validate_locations_rejects_unlocated_node():
    builder = GraphBuilder()
    builder.add_feature(
        FeatureNode(feature_id(FEATURE_TRANSACTOR, "T"), FEATURE_TRANSACTOR, "T")
    )
    with pytest.raises(ValueError, match="no source location"):
        bg.validate_locations(builder, REPO_ROOT)


def test_validate_locations_rejects_misaligned_span():
    """The name-in-span check is what catches an off-by-N line number."""
    from graph import LOC_MACRO, Location

    builder = GraphBuilder()
    builder.add_feature(
        FeatureNode(
            feature_id(FEATURE_TRANSACTOR, "Payment"),
            FEATURE_TRANSACTOR,
            "Payment",
            # Line 1 of transactions.macro is the licence header, not the row.
            locations=[Location(bg.TRANSACTIONS_MACRO, 1, 1, LOC_MACRO)],
        )
    )
    with pytest.raises(ValueError, match="misaligned"):
        bg.validate_locations(builder, REPO_ROOT)


def test_validate_locations_rejects_out_of_bounds_span():
    from graph import LOC_MACRO, Location

    builder = GraphBuilder()
    builder.add_feature(
        FeatureNode(
            feature_id(FEATURE_TRANSACTOR, "Payment"),
            FEATURE_TRANSACTOR,
            "Payment",
            locations=[Location(bg.TRANSACTIONS_MACRO, 999_998, 999_999, LOC_MACRO)],
        )
    )
    with pytest.raises(ValueError, match="past end of file"):
        bg.validate_locations(builder, REPO_ROOT)


def test_invariant_nodes_carry_their_enforcement_files(outputs):
    """The enum row says a bit exists; the checks that consult it are what a PR
    edits. Without these an amendment x invariant change maps to nothing."""
    graph, _ = outputs
    invariants = [r for r in graph["resources"] if r["kind"] == "invariant"]
    assert invariants
    for node in invariants:
        impls = [loc for loc in node["locations"] if loc["role"] == "impl"]
        assert impls, f"{node['id']} has no enforcement file"
        assert all("tx/invariants/" in loc["file"] for loc in impls), node["id"]


def test_ord_privilege_mask_is_resolved(outputs):
    """`hasPrivilege(tx, CreateAcct | CreatePseudoAcct)` is the only site for
    CreateAcct; a scan that required a single bare name missed it entirely."""
    graph, _ = outputs
    create_acct = next(
        r for r in graph["resources"] if r["id"] == "resource:invariant:CreateAcct"
    )
    assert any(loc["role"] == "impl" for loc in create_acct["locations"])


def test_fork_carries_its_state_enum_span(outputs):
    """A new FeePayerType value changes getFeePayer's boundary states, so that
    edit must reach the fork even though the enum lives in a header outside the
    scan scope."""
    graph, _ = outputs
    fork = next(r for r in graph["resources"] if r["id"] == "resource:fork:getFeePayer")
    enums = [loc for loc in fork["locations"] if loc["role"] == "state_enum"]
    assert len(enums) == 1
    span = enums[0]
    body = "\n".join(
        (REPO_ROOT / span["file"])
        .read_text()
        .splitlines()[span["start_line"] - 1 : span["end_line"]]
    )
    assert "FeePayerType" in body
    assert set(fork["state_space"]) <= set(body.replace(",", " ").split())


def test_conflicting_node_definitions_are_fatal():
    """Two extractors disagreeing about a node must not silently resolve to
    whichever ran first."""
    from graph import LOC_MACRO, Location

    builder = GraphBuilder()
    loc = [Location(bg.TRANSACTIONS_MACRO, 27, 41, LOC_MACRO)]
    node_id = feature_id(FEATURE_TRANSACTOR, "Payment")
    builder.add_feature(
        FeatureNode(node_id, FEATURE_TRANSACTOR, "Payment", locations=loc)
    )
    # Identical re-add is normal (extractors run in passes).
    builder.add_feature(
        FeatureNode(node_id, FEATURE_TRANSACTOR, "Payment", locations=loc)
    )
    with pytest.raises(ValueError, match="conflicting definitions"):
        builder.add_feature(
            FeatureNode(
                node_id, FEATURE_TRANSACTOR, "Payment", wrapper=True, locations=loc
            )
        )


def test_impl_overrides_reject_a_scalar_value(tmp_path):
    """The obvious YAML mistake — one path without a list — used to iterate the
    string character by character."""
    bad = tmp_path / "overrides.yml"
    bad.write_text("Payment: src/libxrpl/tx/transactors/payment/Payment.cpp\n")
    with pytest.raises(ValueError, match="must be a list of paths"):
        bg.load_impl_overrides(bad)


def test_resolve_out_paths_json_and_dir(tmp_path):
    g, i = bg.resolve_out_paths(str(tmp_path / "graph.json"))
    assert g == tmp_path / "graph.json"
    assert i == tmp_path / "interactions.json"

    out_dir = tmp_path / "outdir"
    g2, i2 = bg.resolve_out_paths(str(out_dir))
    assert g2 == out_dir / "graph.json"
    assert i2 == out_dir / "interactions.json"
    assert out_dir.is_dir()
