"""Macro parsing: synthetic edge cases plus assertions against the real files.

Expected counts are re-derived from the source files with independent regexes
(not hardcoded and not reusing the code under test), so a real add/remove of a
transactor or amendment stays green while a parser regression fails.
"""

import re
from pathlib import Path

import pytest

from common_fields import parse_common_fields
from graph import FEATURE_AMENDMENT, FEATURE_TRANSACTOR, RESOURCE_SFIELD, GraphBuilder
from macro_extractor import extract_macros, parse_amendments

REPO_ROOT = Path(__file__).resolve().parents[3]
TRANSACTIONS = REPO_ROOT / "include/xrpl/protocol/detail/transactions.macro"
FEATURES = REPO_ROOT / "include/xrpl/protocol/detail/features.macro"
TXFORMATS = REPO_ROOT / "src/libxrpl/protocol/TxFormats.cpp"
SFIELDS = REPO_ROOT / "include/xrpl/protocol/detail/sfields.macro"


def _expected_transactors() -> int:
    return len(re.findall(r"^TRANSACTION\(", TRANSACTIONS.read_text(), re.M))


def _expected_amendments() -> int:
    n = 0
    for line in FEATURES.read_text().splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        if re.match(r"XRPL_(FEATURE|FIX)\s*\(", stripped):
            n += 1
    return n


# --- synthetic amendment parsing edge cases ---------------------------------


def test_amendments_skip_retire_and_comments(tmp_path):
    macro = tmp_path / "features.macro"
    macro.write_text(
        "\n".join(
            [
                "XRPL_FEATURE(Alpha,   Supported::Yes, VoteBehavior::DefaultNo)",
                "XRPL_FIX    (Beta,    Supported::Yes, VoteBehavior::DefaultNo)",
                "// XRPL_FEATURE(Example, Supported::yes, VoteBehavior::Obsolete)",
                "XRPL_RETIRE_FEATURE(OldOne)",
                "XRPL_RETIRE_FIX(OldFix)",
            ]
        )
    )
    names, globals_map = parse_amendments(macro)
    assert names == {"Alpha", "Beta"}
    # Space-aligned XRPL_FIX must still be captured, prefixed as `fix`.
    assert globals_map == {"featureAlpha": "Alpha", "fixBeta": "Beta"}


# --- real-file feature extraction -------------------------------------------


@pytest.fixture(scope="module")
def built():
    common = parse_common_fields(TXFORMATS)
    builder = GraphBuilder()
    globals_map = extract_macros(
        builder, REPO_ROOT, TRANSACTIONS, FEATURES, SFIELDS, common
    )
    return builder, globals_map


def test_transactor_and_amendment_counts(built):
    builder, _ = built
    transactors = [f for f in builder.features.values() if f.kind == FEATURE_TRANSACTOR]
    amendments = [f for f in builder.features.values() if f.kind == FEATURE_AMENDMENT]
    assert len(transactors) == _expected_transactors()
    assert len(amendments) == _expected_amendments()


def test_batch_is_wrapper_notdelegable(built):
    builder, _ = built
    batch = builder.feature_by_name(FEATURE_TRANSACTOR, "Batch")
    assert batch is not None
    assert batch.wrapper is True
    assert batch.delegable is False
    assert batch.amendment == "BatchV1_1"
    assert "sfRawTransactions" in batch.fields


def test_empty_fields_transactor(built):
    builder, _ = built
    did = builder.feature_by_name(FEATURE_TRANSACTOR, "DIDDelete")
    assert did is not None
    assert did.fields == []


def test_genesis_transactor_has_no_amendment(built):
    builder, _ = built
    # Payment is enabled from genesis (uint256{} sentinel -> None).
    payment = builder.feature_by_name(FEATURE_TRANSACTOR, "Payment")
    assert payment.amendment is None


def test_amendment_globals_map(built):
    _, globals_map = built
    assert globals_map["featureBatchV1_1"] == "BatchV1_1"
    assert globals_map["featurePermissionDelegationV1_1"] == "PermissionDelegationV1_1"


def test_shared_sfield_resources(built):
    builder, _ = built
    common = parse_common_fields(TXFORMATS)
    shared = [r for r in builder.resources.values() if r.kind == RESOURCE_SFIELD]
    assert shared, "expected some shared per-tx SField resources"
    for res in shared:
        # Never a common field, and shared by >=2 transactors (>=2 consumer edges).
        assert res.name not in common
        assert res.signal == "low"
        carriers = [e for e in builder.edges if e.dst == res.id]
        assert len(carriers) >= 2
    # A field carried by exactly one transactor must not appear as a resource.
    names = {r.name for r in shared}
    assert "sfRawTransactions" not in names  # only Batch carries it


def test_bad_delegability_fails_loudly(tmp_path):
    txns = tmp_path / "transactions.macro"
    txns.write_text(
        "TRANSACTION(ttTEST, 0, TestTx, Delegation::Bogus, uint256{}, NoPriv, ({}))\n"
    )
    feats = tmp_path / "features.macro"
    feats.write_text("XRPL_FEATURE(Alpha, Supported::Yes, VoteBehavior::DefaultNo)\n")
    sfields = tmp_path / "sfields.macro"
    sfields.write_text("TYPED_SFIELD(sfAlpha, UINT8, 1)\n")
    with pytest.raises(ValueError, match="delegab"):
        extract_macros(GraphBuilder(), tmp_path, txns, feats, sfields, set())
