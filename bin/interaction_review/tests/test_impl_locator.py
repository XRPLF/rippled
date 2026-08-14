"""Transactor implementation resolution: the three derivation rules, the
failure mode when none apply, and the override escape hatch."""

import pytest

import impl_locator as il
from graph import LOC_IMPL


def _tree(root, files: dict[str, str]):
    """Materialize {repo-relative path: text} under root."""
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return root


SRC = il.TRANSACTOR_TREES[0]
INC = il.TRANSACTOR_TREES[1]


@pytest.fixture()
def repo(tmp_path):
    return _tree(
        tmp_path,
        {
            # Rule 1: named after the transactor.
            f"{SRC}/payment/Payment.cpp": "Payment::doApply() {}\n",
            f"{INC}/payment/Payment.h": "class Payment {};\n",
            # Rule 2: several classes share one file.
            # clang-format puts the return type on its own line, so a
            # definition starts at column 0. `AMMWithdraw::withdraw(` here is a
            # *call*, indented, and must not attribute this file to AMMWithdraw.
            f"{SRC}/bridge/Bridge.cpp": (
                "NotTEC\n"
                "XChainCommit::preflight() {\n"
                "    AMMWithdraw::withdraw(view);\n"
                "}\n"
                "class BridgeModify {};\n"
            ),
            # Rule 3: alias to a class defined elsewhere, and a chained alias.
            f"{INC}/bridge/Bridge.h": (
                "using XChainModifyBridge = BridgeModify;\n"
                "using AliasOfAlias = XChainModifyBridge;\n"
            ),
            # A class named only in a comment must not be indexed.
            f"{SRC}/misc/Misc.cpp": "// class GhostTransactor is not real\n",
            # An alias cycle must terminate rather than hang.
            f"{INC}/misc/Cycle.h": "using Ping = Pong;\nusing Pong = Ping;\n",
        },
    )


@pytest.fixture()
def index(repo):
    return il.build_index(repo)


def test_rule1_stem_match(index, repo):
    files = il.resolve_files("Payment", index)
    assert {p.name for p in files} == {"Payment.cpp", "Payment.h"}


def test_rule2_symbol_match(index):
    # XChainCommit has no file of its own; it is found by its member definition.
    assert [p.name for p in il.resolve_files("XChainCommit", index)] == ["Bridge.cpp"]


def test_rule3_alias_match(index):
    # XChainModifyBridge is a `using` alias for BridgeModify, declared in Bridge.cpp.
    assert [p.name for p in il.resolve_files("XChainModifyBridge", index)] == [
        "Bridge.cpp"
    ]


def test_alias_chain_is_followed(index):
    assert [p.name for p in il.resolve_files("AliasOfAlias", index)] == ["Bridge.cpp"]


def test_alias_cycle_terminates(index):
    # Must return empty rather than loop forever.
    assert il.resolve_files("Ping", index) == []


def test_commented_class_is_not_indexed(index):
    assert il.resolve_files("GhostTransactor", index) == []


def test_qualified_call_does_not_attribute_the_calling_file(index):
    """Regression: an unanchored `Name::method(` matched call sites too, so a
    file that merely *calls* AMMWithdraw::withdraw was attributed to the
    AMMWithdraw transactor. 8 real transactors were affected."""
    assert il.resolve_files("AMMWithdraw", index) == []


def test_unresolved_transactor_is_fatal(repo):
    with pytest.raises(ValueError, match="no implementation file found"):
        il.locate_transactor_impls(repo, ["Payment", "NoSuchTransactor"])


def test_locations_are_whole_file_impl_role(repo):
    located = il.locate_transactor_impls(repo, ["Payment"])
    locs = located["Payment"]
    assert {loc.role for loc in locs} == {LOC_IMPL}
    assert all(loc.start_line == 1 for loc in locs)
    # Sorted and repo-relative, so the graph is stable across runs.
    assert [loc.file for loc in locs] == sorted(loc.file for loc in locs)
    assert all(not loc.file.startswith("/") for loc in locs)


def test_override_resolves_and_validates(repo):
    located = il.locate_transactor_impls(
        repo,
        ["Ghost"],
        overrides={"Ghost": [f"{SRC}/misc/Misc.cpp"]},
    )
    assert [loc.file for loc in located["Ghost"]] == [f"{SRC}/misc/Misc.cpp"]

    with pytest.raises(ValueError, match="does not exist"):
        il.locate_transactor_impls(repo, ["Ghost"], overrides={"Ghost": ["nope.cpp"]})


def test_unused_overrides_reported():
    assert il.unused_overrides({"Gone": ["a"], "Here": ["b"]}, ["Here"]) == ["Gone"]


def test_missing_tree_is_fatal(tmp_path):
    with pytest.raises(FileNotFoundError, match="transactor tree"):
        il.build_index(tmp_path)
