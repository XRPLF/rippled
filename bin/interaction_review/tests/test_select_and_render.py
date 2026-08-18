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
from review_clusters import build_review_clusters

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
            {"id": "feature:transactor:NFTokenBurn", "name": "NFTokenBurn"},
            {"id": "feature:transactor:NFTokenMint", "name": "NFTokenMint"},
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
            {
                "id": "resource:invariant:ChangeNftCounts",
                "name": "ChangeNftCounts",
                "signal": "medium",
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
            {
                "kind": "consumer",
                "src": "feature:transactor:NFTokenBurn",
                "dst": "resource:invariant:ChangeNftCounts",
                "via": "ChangeNftCounts",
            },
            {
                "kind": "consumer",
                "src": "feature:transactor:NFTokenMint",
                "dst": "resource:invariant:ChangeNftCounts",
                "via": "ChangeNftCounts",
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
            "vias": [
                ["wrapper"] if name == "Batch" else ["sfDelegate"] for name in features
            ],
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
            pair(
                "ChangeNftCounts",
                "invariant",
                "medium",
                ("NFTokenBurn", "NFTokenMint"),
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


def add_fork_mediator_pair(
    graph_input: dict,
    interaction_input: dict,
    *,
    owner: str,
    owner_vias: tuple[str, ...],
) -> None:
    """Add an amendment owner paired with Batch on the fixture fork."""
    owner_id = f"feature:amendment:{owner}"
    graph_input["features"].append({"id": owner_id, "name": owner})
    graph_input["edges"].append(
        {
            "kind": "mediator",
            "src": owner_id,
            "dst": FORK,
            "via": owner_vias[0],
        }
    )
    interaction_input["interactions"].append(
        {
            "resource": "getFeePayer",
            "resource_kind": "fork",
            "signal": "high",
            "kind": "mediator×mediator",
            "features": [owner, "Batch"],
            "roles": ["mediator", "mediator"],
            "vias": [list(owner_vias), ["wrapper"]],
            "boundary_states": [],
        }
    )


def test_untouched_diff_selects_nothing():
    report = select()
    assert report["groups"] == []
    assert report["summary"]["candidates"] == 0
    # A report that covers nothing must still say what it does not cover.
    assert report["caveats"]


def test_editing_a_fork_keeps_mediator_pairs_and_summarizes_pass_throughs():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    assert [g["resource"] for g in report["groups"]] == ["getFeePayer"]
    pairs = {tuple(i["features"]) for i in report["groups"][0]["interactions"]}
    assert pairs == {("PermissionDelegation", "Batch")}
    assert report["groups"][0]["consumer_cohort"] == {
        "mediators": ["Batch"],
        "wrappers": ["Batch"],
        "consumers": ["Payment"],
        "pair_count": 1,
    }
    assert report["summary"]["candidates"] == 2
    assert report["summary"]["cohort_pairs"] == 1
    assert report["summary"]["truncated"] == 0
    assert report["groups"][0]["interactions"][0]["features"] == [
        "PermissionDelegation",
        "Batch",
    ]
    pass_through = next(
        candidate
        for candidate in report["investigation_candidates"]
        if candidate["features"] == ["Batch", "Payment"]
    )
    assert pass_through["resource"] == "getFeePayer"
    assert (
        "feature_ids" not in pass_through
    ), "legacy display names must not be promoted to canonical graph IDs"


def test_a_cohort_only_fork_still_renders_a_group():
    only_pass_through = interactions()
    only_pass_through["interactions"] = [
        item
        for item in only_pass_through["interactions"]
        if item["resource"] == "getFeePayer" and item["roles"].count("consumer") == 1
    ]
    report = sel.select(
        graph(),
        only_pass_through,
        touched(node(FORK, "fork", "getFeePayer", signal="high")),
    )
    assert len(report["groups"]) == 1
    assert report["groups"][0]["interactions"] == []
    assert report["groups"][0]["consumer_cohort"]["consumers"] == ["Payment"]
    body = render_comment.render(report)
    assert "No model comments are available for this change." in body
    assert "getFeePayer" not in body
    assert "Payment" not in body
    assert "| features |" not in body


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
    assert "featureFoo" in why and "not recorded" in why


def test_feature_new_sfield_promotes_only_its_owner_pair():
    graph_input = graph()
    interaction_input = interactions()
    add_fork_mediator_pair(
        graph_input,
        interaction_input,
        owner="Sponsor",
        owner_vias=("sfSponsor", "sfSponsorSignature"),
    )

    def run(new_levers=()):
        return sel.select(
            graph_input,
            interaction_input,
            touched(
                node(
                    "feature:transactor:Batch",
                    "transactor",
                    "Batch",
                    new_levers=new_levers,
                )
            ),
        )

    def scores(report):
        return {
            tuple(item["features"]): item["score"]
            for item in report["groups"][0]["interactions"]
        }

    plain = run()
    promoted = run(("sfSponsor",))
    plain_scores = scores(plain)
    promoted_scores = scores(promoted)

    assert promoted_scores[("Sponsor", "Batch")] == (
        plain_scores[("Sponsor", "Batch")] + sel.FEATURE_LEVER_OWNER_SCORE
    )
    assert (
        promoted_scores[("PermissionDelegation", "Batch")]
        == plain_scores[("PermissionDelegation", "Batch")]
    )
    assert promoted["groups"][0]["interactions"][0]["features"] == [
        "Sponsor",
        "Batch",
    ]
    sponsor = promoted["groups"][0]["interactions"][0]
    why = " ".join(sponsor["why"])
    assert "sfSponsor" in why and "directly to `Sponsor`" in why


@pytest.mark.parametrize("lever", ["featureBatchV1_1", "fixBatchV1_1"])
def test_feature_new_amendment_global_matches_normalized_owner(lever):
    graph_input = graph()
    interaction_input = interactions()
    add_fork_mediator_pair(
        graph_input,
        interaction_input,
        owner="BatchV1_1",
        owner_vias=("amendment",),
    )
    report = sel.select(
        graph_input,
        interaction_input,
        touched(
            node(
                "feature:transactor:Batch",
                "transactor",
                "Batch",
                new_levers=(lever,),
            )
        ),
    )
    pair = next(
        item
        for item in report["groups"][0]["interactions"]
        if item["features"] == ["BatchV1_1", "Batch"]
    )
    assert pair["score"] == (
        sel.SIGNAL_SCORE["high"]
        + sel.KIND_SCORE["mediator×mediator"]
        + sel.FEATURE_MATCH_SCORE["span"]
        + sel.FEATURE_LEVER_OWNER_SCORE
    )
    assert lever in " ".join(pair["why"])


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
        node(FORK, "fork", "getFeePayer", signal="high"),
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
        max_per_resource=1,
    )
    assert len(report["groups"][0]["interactions"]) == 1
    assert report["groups"][0]["omitted"] == 1
    assert report["summary"]["truncated"] == 1


def test_presentation_caps_do_not_remove_cluster_locations():
    uncapped = select(
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
        node("feature:transactor:Batch", "transactor", "Batch", "file"),
        max_interactions=99,
        max_per_resource=99,
        max_resources=99,
    )
    report = select(
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
        node("feature:transactor:Batch", "transactor", "Batch", "file"),
        max_interactions=1,
        max_per_resource=1,
        max_resources=1,
    )

    assert len(report["groups"]) == 1
    assert sum(len(group["interactions"]) for group in report["groups"]) == 1
    assert report["investigation_candidates"] == uncapped["investigation_candidates"]
    assert build_review_clusters(report, max_items=99) == build_review_clusters(
        uncapped, max_items=99
    )
    cluster = next(
        cluster
        for cluster in build_review_clusters(report, max_items=99)
        if sorted(cluster["features"]) == ["Batch", "Payment"]
    )
    assert {location["resource"] for location in cluster["locations"]} == {
        "getFeePayer",
        "sfAmount",
    }


def test_a_single_holder_invariant_is_one_judgeable_authorization_unit():
    graph_input = graph()
    graph_input["features"].append(
        {"id": "feature:transactor:AMMClawback", "name": "AMMClawback"}
    )
    graph_input["resources"].append(
        {
            "id": "resource:invariant:OverrideFreeze",
            "kind": "invariant",
            "name": "OverrideFreeze",
            "signal": "medium",
            "state_space": [],
        }
    )
    graph_input["edges"].append(
        {
            "kind": "consumer",
            "src": "feature:transactor:AMMClawback",
            "dst": "resource:invariant:OverrideFreeze",
            "via": "OverrideFreeze",
        }
    )
    report = sel.select(
        graph_input,
        interactions(),  # no pair can be enumerated for a one-holder invariant
        touched(
            node(
                "resource:invariant:OverrideFreeze",
                "invariant",
                "OverrideFreeze",
                signal="medium",
            )
        ),
    )

    assert [group["resource"] for group in report["groups"]] == ["OverrideFreeze"]
    assert report["groups"][0]["authorized_features"] == ["AMMClawback"]
    assert report["groups"][0]["interactions"] == []
    assert report["summary"]["candidates"] == 0
    assert report["summary"]["investigation_candidates"] == 1
    assert report["summary"]["investigation_candidates"] == len(
        report["investigation_candidates"]
    )
    clusters = build_review_clusters(report, max_items=1)
    assert len(clusters) == 1
    assert clusters[0]["kind"] == "invariant"
    assert clusters[0]["locations"][0]["evidence"]


def test_invariant_group_carries_the_complete_authorized_set():
    report = select(
        node(
            "resource:invariant:ChangeNftCounts",
            "invariant",
            "ChangeNftCounts",
            signal="medium",
        )
    )
    group = report["groups"][0]
    assert group["authorized_features"] == ["NFTokenBurn", "NFTokenMint"]
    assert group["consumer_cohort"] is None


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
    assert "3 changed files" in joined
    assert "Static selection does not check test coverage" in joined


def test_output_validates_against_the_schema():
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"]),
        node("feature:transactor:Payment", "transactor", "Payment", "file"),
    )
    sel.validate(report, MODULE_DIR / "selected.schema.json")


def test_schema_keeps_group_only_selected_artifacts_compatible():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    report.pop("investigation_candidates")
    report["summary"].pop("investigation_candidates")

    sel.validate(report, MODULE_DIR / "selected.schema.json")


@pytest.mark.parametrize(
    "mutation",
    [
        lambda candidate: candidate.update(
            resource_kind="invariant", authorized_features=["Batch"]
        ),
        lambda candidate: candidate["roles"].pop(),
        lambda candidate: candidate["vias"].pop(),
    ],
)
def test_schema_rejects_malformed_pair_investigation_candidates(mutation):
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    candidate = report["investigation_candidates"][0]
    mutation(candidate)

    with pytest.raises(ValueError, match="selected.json failed schema validation"):
        sel.validate(report, MODULE_DIR / "selected.schema.json")


def test_schema_rejects_an_invariant_candidate_with_pair_roles():
    graph_input = graph()
    graph_input["resources"].append(
        {
            "id": "resource:invariant:OverrideFreeze",
            "kind": "invariant",
            "name": "OverrideFreeze",
            "signal": "medium",
            "state_space": [],
        }
    )
    report = sel.select(
        graph_input,
        interactions(),
        touched(
            node(
                "resource:invariant:OverrideFreeze",
                "invariant",
                "OverrideFreeze",
                signal="medium",
            )
        ),
    )
    report["investigation_candidates"][0]["roles"] = ["consumer", "consumer"]

    with pytest.raises(ValueError, match="selected.json failed schema validation"):
        sel.validate(report, MODULE_DIR / "selected.schema.json")


def test_schema_accepts_a_bound_worktree_snapshot():
    report = select()
    report["source_binding"] = {
        "version": 1,
        "mode": "worktree",
        "base": "0" * 40,
        "head": "worktree",
        "checkout_head": "1" * 40,
        "tracked_diff_sha256": "2" * 64,
        "fingerprint": "3" * 64,
    }
    sel.validate(report, MODULE_DIR / "selected.schema.json")


def test_schema_mismatch_fails_loudly(tmp_path):
    path = tmp_path / "graph.json"
    path.write_text(json.dumps({"schema": 99}))
    with pytest.raises(ValueError, match="schema version"):
        sel._require_schema(path, 1, "graph.json")


def test_cli_captures_the_source_binding(monkeypatch, tmp_path):
    graph_path = tmp_path / "graph.json"
    interactions_path = tmp_path / "interactions.json"
    touched_path = tmp_path / "touched.json"
    selected_path = tmp_path / "selected.json"
    graph_path.write_text(json.dumps(graph()))
    interactions_path.write_text(json.dumps(interactions()))
    touched_path.write_text(json.dumps(touched()))
    binding = {
        "version": 1,
        "mode": "worktree",
        "base": "0" * 40,
        "head": "worktree",
        "checkout_head": "1" * 40,
        "tracked_diff_sha256": "2" * 64,
        "fingerprint": "3" * 64,
    }
    captured = {}

    class FakeSnapshot:
        @classmethod
        def capture(cls, repo_root, *, base, head):
            captured.update(repo_root=repo_root, base=base, head=head)
            return cls()

        def binding_metadata(self):
            return binding

    monkeypatch.setattr(sel, "SourceSnapshot", FakeSnapshot)

    assert (
        sel.main(
            [
                "--graph",
                str(graph_path),
                "--interactions",
                str(interactions_path),
                "--touched",
                str(touched_path),
                "--out",
                str(selected_path),
                "--repo-root",
                str(tmp_path),
            ]
        )
        == 0
    )
    report = json.loads(selected_path.read_text())
    assert report["source_binding"] == binding
    assert captured == {
        "repo_root": Path(tmp_path),
        "base": "0" * 40,
        "head": "worktree",
    }


def test_unjudged_comment_hides_static_candidate_details():
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"])
    )
    body = render_comment.render(report)
    assert body.startswith(render_comment.MARKER)
    assert "No model comments are available for this change." in body
    assert "PermissionDelegation" not in body
    assert "Batch" not in body
    assert "Account" not in body
    assert "Delegate" not in body
    assert "featureFoo" not in body
    assert "f.cpp" not in body
    assert "| features |" not in body


def test_unjudged_comment_does_not_summarize_transaction_consumers():
    body = render_comment.render(
        select(node(FORK, "fork", "getFeePayer", signal="high"))
    )
    assert "transaction type" not in body
    assert "feature/transaction-type combination" not in body
    assert "Batch" not in body
    assert "Payment" not in body


def test_unjudged_comment_does_not_render_repeated_static_pairs():
    graph_input = graph()
    graph_input["resources"].append(
        {
            "id": "resource:fork:checkFee",
            "name": "checkFee",
            "signal": "high",
        }
    )
    graph_input["edges"] += [
        {
            "kind": "wrapper",
            "src": "feature:transactor:Batch",
            "dst": "resource:fork:checkFee",
            "via": "sfRawTransactions",
        },
        {
            "kind": "consumer",
            "src": "feature:transactor:Payment",
            "dst": "resource:fork:checkFee",
            "via": "sfFee",
        },
    ]
    interaction_input = interactions()
    duplicate = json.loads(
        json.dumps(
            next(
                item
                for item in interaction_input["interactions"]
                if item["resource"] == "getFeePayer"
                and item["roles"].count("consumer") == 1
            )
        )
    )
    duplicate["resource"] = "checkFee"
    duplicate["boundary_states"] = []
    interaction_input["interactions"].append(duplicate)
    report = sel.select(
        graph_input,
        interaction_input,
        touched(
            node(
                "feature:transactor:Payment",
                "transactor",
                "Payment",
                "file",
            )
        ),
    )
    body = render_comment.render(report)
    pair = "`Batch` (wraps transactions through it) × `Payment` (uses it)"
    assert pair not in body
    assert "checkFee" not in body
    assert "cross-feature checks" not in body
    assert "No model comments are available for this change." in body


def test_unjudged_invariant_does_not_render_the_static_authorization_rule():
    body = render_comment.render(
        select(
            node(
                "resource:invariant:ChangeNftCounts",
                "invariant",
                "ChangeNftCounts",
                signal="medium",
            )
        )
    )
    assert "ChangeNftCounts" not in body
    assert "NFTokenBurn" not in body
    assert "protected branch" not in body
    assert "No model comments are available for this change." in body
    assert "| features |" not in body


def test_rendered_comment_is_explicit_when_there_are_no_model_comments():
    body = render_comment.render(select())
    assert "No model comments are available for this change." in body
    assert render_comment.MARKER in body


def test_rendered_comment_uses_no_internal_vocabulary():
    """The reader is a rippled engineer, not someone who has read DESIGN.md.

    Every one of these is a term of art in this tool's model. They may appear in
    the code, the schemas, and the docs; they must not reach the comment, or the
    reader has to learn the model before they can act on the report.
    """
    report = select(
        node(FORK, "fork", "getFeePayer", signal="high", new_levers=["featureFoo"])
    )
    body = render_comment.render(report).lower()
    for term in (
        "mediator",
        "consumer",
        "graph node",
        "lever",
        "in scope",
        "boundary state",
        "resource",
        "candidate",
        "span",
    ):
        assert term not in body, f"internal vocabulary leaked into the comment: {term}"


def test_rendered_comment_is_truncated_to_fit_a_github_comment():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    report["judgements"] = [
        {
            "features": ["Batch", "PermissionDelegation"],
            "locations": [
                {
                    "resource_kind": "fork",
                    "resource": "getFeePayer",
                    "tier": "review",
                    "score": 100,
                }
            ],
            "behavior": "broken",
            "coverage": "missing",
            "verdict": "gap",
            "confidence": "high",
            "summary": "A concise summary.",
            "detail": "x" * (render_comment.MAX_BODY + 1000),
            "citations": [],
            "states_unreached": [],
            "error": None,
        }
    ]
    body = render_comment.render(report)
    assert len(body.encode("utf-8")) <= render_comment.MAX_BODY
    assert "Comment truncated" in body


def test_rendered_comment_applies_the_limit_in_utf8_bytes():
    report = select(node(FORK, "fork", "getFeePayer", signal="high"))
    report["judgements"] = [
        {
            "features": ["Batch", "PermissionDelegation"],
            "locations": [
                {
                    "resource_kind": "fork",
                    "resource": "getFeePayer",
                    "tier": "review",
                    "score": 100,
                }
            ],
            "behavior": "broken",
            "coverage": "missing",
            "verdict": "gap",
            "confidence": "high",
            "summary": "A concise summary.",
            "detail": "界" * render_comment.MAX_BODY,
            "citations": [],
            "states_unreached": [],
            "error": None,
        }
    ]

    body = render_comment.render(report)

    assert len(body.encode("utf-8")) <= render_comment.MAX_BODY
    assert "Comment truncated" in body
    assert "Experimental and advisory only" in body
