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
        REPO_ROOT / bg.TRANSACTIONS_MACRO,
        REPO_ROOT / bg.FEATURES_MACRO,
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
    builder = GraphBuilder()
    builder.add_feature(
        FeatureNode(feature_id(FEATURE_TRANSACTOR, "T"), FEATURE_TRANSACTOR, "T")
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


def test_resolve_out_paths_json_and_dir(tmp_path):
    g, i = bg.resolve_out_paths(str(tmp_path / "graph.json"))
    assert g == tmp_path / "graph.json"
    assert i == tmp_path / "interactions.json"

    out_dir = tmp_path / "outdir"
    g2, i2 = bg.resolve_out_paths(str(out_dir))
    assert g2 == out_dir / "graph.json"
    assert i2 == out_dir / "interactions.json"
    assert out_dir.is_dir()
