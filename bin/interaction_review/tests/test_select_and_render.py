"""Tests for interaction selection and comment rendering.

Fixtures are hand-built miniature graphs rather than the real artifacts: the
selection rules are about scoring and capping, and a two-resource graph makes an
off-by-one in a cap visible where the real 4350-interaction set would not.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

import render_comment
import select_interactions as sel

HERE = Path(__file__).resolve().parent
MODULE_DIR = HERE.parent


def graph() -> dict:
    """Two features on a high-signal fork and a low-signal shared field.

    `Batch` exists as both a transactor and an amendment so the name-to-id
    resolution through the edge list is actually exercised.
    """
    return {
        "schema": 1,
        "vocabulary": {},
        "features": [
            {"id": "feature:transactor:Batch", "name": "Batch"},
            {"id": "feature:amendment:Batch", "name": "Batch"},
            {
                "id": "feature:amendment:PermissionDelegation",
                "name": "PermissionDelegation",
            },
            {"id": "feature:transactor:Payment", "name": "Payment"},
        ],
        "resources": [
            {
                "id": "resource:fork:getFeePayer",
                "name": "getFeePayer",
                "signal": "high",
            },
            {
                "id": "resource:shared_sfield:sfAmount",
                "name": "sfAmount",
                "signal": "low",
            },
        ],
        "edges": [
            {
                "kind": "wrapper",
                "src": "feature:transactor:Batch",
                "dst": "resource:fork:getFeePayer",
                "via": "sfRawTransactions",
            },
            {
                "kind": "mediator",
                "src": "feature:amendment:PermissionDelegation",
                "dst": "resource:fork:getFeePayer",
                "via": "sfDelegate",
            },
            {
                "kind": "consumer",
                "src": "feature:transactor:Payment",
                "dst": "resource:fork:getFeePayer",
                "via": "sfFee",
            },
            {
                "kind": "consumer",
                "src": "feature:transactor:Payment",
                "dst": "resource:shared_sfield:sfAmount",
                "via": "sfAmount",
            },
            {
                "kind": "consumer",
                "src": "feature:transactor:Batch",
                "dst": "resource:shared_sfield:sfAmount",
                "via": "sfAmount",
            },
        ],
    }


def interactions() -> dict:
    def pair(resource, kind, signal, features, roles, states=()):
        return {
            "resource": resource,
            "resource_kind": kind,
            "signal": signal,
            "kind": f"{roles[0]}×{roles[1]}",
            "features": list(features),
            "roles": list(roles),
            "vias": [["sfDelegate"], ["sfRawTransactions"]],
            "boundary_states": list(states),
        }

    return {
        "summary": {},
        "interactions": [
            pair(
                "getFeePayer",
                "fork",
                "high",
                ("PermissionDelegation", "Batch"),
                ("mediator", "mediator"),
                ("Account", "Delegate"),
            ),
            pair(
                "getFeePayer",
                "fork",
                "high",
                ("Batch", "Payment"),
                ("mediator", "consumer"),
                ("Account", "Delegate"),
            ),
            pair(
                "sfAmount",
                "shared_sfield",
                "low",
                ("Batch", "Payment"),
                ("consumer", "consumer"),
            ),
        ],
    }


def touched(*entries, **summary) -> dict:
    base = {
        "changed_files": 1,
        "unmapped_files": 0,
        "touched_nodes": len(entries),
        "off_span_files": 0,
        "structural_files": 0,
        "by_kind": {},
        "by_match": {},
    }
    base.update(summary)
    return {
        "schema": 1,
        "base": "0" * 40,
        "head": "worktree",
        "summary": base,
        "touched": list(entries),
        "off_span_changes": [],
        "structural_changes": [],
        "unmapped_files": [],
    }


def node(
    node_id, kind, name, match="span", *, new_levers=(), signal=None, file="f.cpp"
):
    return {
        "id": node_id,
        "kind": kind,
        "name": name,
        "match": match,
        "signal": signal,
        "known_levers": [],
        "new_levers": list(new_levers),
        "evidence": [
            {"file": file, "role": "definition", "span": [1, 100], "lines": [[10, 12]]}
        ],
    }


FORK = "resource:fork:getFeePayer"


def select(*entries, **kwargs) -> dict:
    return sel.select(graph(), interactions(), touched(*entries), **kwargs)


def test_untouched_diff_selects_nothing():
    report = select()
    assert report["groups"] == []
    assert report["summary"]["candidates"] == 0
    # A report that covers nothing must still say what it does not cover.
    assert report["caveats"]


def test_editing_a_fork_puts_every_pair_on_it_in_scope():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    assert [g["resource"] for g in report["groups"]] == ["getFeePayer"]
    pairs = {tuple(i["features"]) for i in report["groups"][0]["interactions"]}
    assert pairs == {("PermissionDelegation", "Batch"), ("Batch", "Payment")}
    # The mediator×mediator pair outranks mediator×consumer on the same resource.
    assert report["groups"][0]["interactions"][0]["features"] == [
        "PermissionDelegation",
        "Batch",
    ]


def test_mediator_pair_on_an_edited_fork_reaches_the_review_tier():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    top = report["groups"][0]["interactions"][0]
    assert top["tier"] == sel.TIER_REVIEW
    assert top["score"] == (
        sel.SIGNAL_SCORE["high"]
        + sel.KIND_SCORE["mediator×mediator"]
        + sel.RESOURCE_MATCH_SCORE["span"]
        + sel.BOUNDARY_STATE_SCORE
    )


def test_a_new_lever_dominates_the_ranking():
    plain = select(node(FORK, "fork", "getFeePayer", signal="high"))
    lever = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"])
    )
    delta = lever["groups"][0]["score"] - plain["groups"][0]["score"]
    assert delta == sel.NEW_LEVER_SCORE
    assert lever["summary"]["new_levers"] == ["featureFoo"]
    why = " ".join(lever["groups"][0]["interactions"][0]["why"])
    assert "featureFoo" in why and "edge set" in why


def test_low_signal_consumer_pairs_are_dropped_but_counted():
    # Payment alone: its sfAmount pair with Batch is noise, its fork pairs are not.
    report = select(node("feature:transactor:Payment", "transactor", "Payment", "file"))
    assert [g["resource"] for g in report["groups"]] == ["getFeePayer"]
    assert report["summary"]["dropped_low_signal"] == 1


def test_low_signal_pair_is_kept_when_both_endpoints_are_edited():
    report = select(
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
        node("feature:transactor:Batch", "transactor", "Batch", "file"),
    )
    assert "sfAmount" in [g["resource"] for g in report["groups"]]
    assert report["summary"]["dropped_low_signal"] == 0
    sfield = next(g for g in report["groups"] if g["resource"] == "sfAmount")
    assert "both endpoints" in " ".join(sfield["interactions"][0]["why"])


def test_features_resolve_through_edges_not_by_name():
    # The transactor `Batch` is the one edged to the fork, so editing it resolves.
    assert select(node("feature:transactor:Batch", "transactor", "Batch"))["groups"]
    # The amendment of the same name is edged to nothing, so editing it must not
    # inherit the transactor's pairs. Resolving by bare name would conflate them.
    assert select(node("feature:amendment:Batch", "amendment", "Batch"))["groups"] == []
    # And a feature absent from the resource's edges pulls in nothing at all.
    assert select(node("feature:amendment:AMM", "amendment", "AMM"))["groups"] == []


def test_caps_are_reported_never_silent():
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high"), max_per_resource=1
    )
    assert len(report["groups"][0]["interactions"]) == 1
    assert report["groups"][0]["omitted"] == 1
    assert report["summary"]["truncated"] == 1


def test_resource_cap_keeps_the_best_resource_whole():
    report = select(
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
        node("feature:transactor:Batch", "transactor", "Batch", "file"),
        max_resources=1,
    )
    assert [g["resource"] for g in report["groups"]] == ["getFeePayer"]
    assert report["summary"]["omitted_resources"] == 1


def test_off_span_and_structural_changes_become_caveats():
    report_input = touched(node(FORK, "fork", "getFeePayer", signal="high"))
    report_input["off_span_changes"] = [
        {"file": "src/libxrpl/tx/Transactor.cpp", "lines": [[61, 61]], "hosts": [FORK]}
    ]
    report_input["structural_changes"] = [
        {"file": "src/libxrpl/tx/gone.cpp", "kind": "deleted", "hosted": [FORK]}
    ]
    report_input["summary"]["unmapped_files"] = 3
    report = sel.select(graph(), interactions(), report_input)
    joined = " ".join(report["caveats"])
    assert "Transactor.cpp" in joined
    assert "gone.cpp" in joined
    assert "3 changed file(s)" in joined
    assert "not** checked" in joined


def test_output_validates_against_the_schema():
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"]),
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
    )
    sel.validate(report, MODULE_DIR / "selected.schema.json")


def test_schema_mismatch_fails_loudly(tmp_path):
    path = tmp_path / "graph.json"
    path.write_text(json.dumps({"schema": 99}))
    with pytest.raises(ValueError, match="schema version"):
        sel._require_schema(path, 1, "graph.json")


def test_rendered_comment_names_the_pair_and_the_boundary_states():
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"])
    )
    body = render_comment.render(report)
    assert body.startswith(render_comment.MARKER)
    assert "`PermissionDelegation` (mediator) × `Batch` (mediator)" in body
    assert "Boundary states: `Account`, `Delegate`" in body
    assert "featureFoo" in body
    assert "f.cpp:10-12" in body
    # The comment must disclaim rather than assert anything about test coverage.
    assert "Test coverage is **not** checked yet" in body
    assert "What this does not cover" in body


def test_rendered_comment_is_explicit_when_nothing_is_in_scope():
    body = render_comment.render(select())
    assert "No feature-interaction boundaries are in scope" in body
    assert render_comment.MARKER in body


def test_rendered_comment_is_truncated_to_fit_a_github_comment():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    report["caveats"].append("x" * (render_comment.MAX_BODY + 1000))
    body = render_comment.render(report)
    assert len(body) <= render_comment.MAX_BODY + 200
    assert "Comment truncated" in body
