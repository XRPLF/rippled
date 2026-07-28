"""Fork extraction: a synthetic TU exercising every access idiom, plus a
check against the real Transactor.cpp parse."""

from pathlib import Path

import pytest

import clang.cindex as ci
from common_fields import parse_common_fields
from fork_extractor import extract_forks, scan_translation_unit

REPO_ROOT = Path(__file__).resolve().parents[3]
TXFORMATS = REPO_ROOT / "src/libxrpl/protocol/TxFormats.cpp"

# getFeePayer references common fields/flag/gate -> discovered as a fork. Each
# lever uses a different access idiom to show the DeclRefExpr sweep is
# idiom-agnostic: predicate, subscript, getFieldVL, getAccountID; the flag via
# isFlag and the gate via rules().enabled. `noFork` references only a non-common
# field, so it must NOT be discovered.
FIXTURE = """
struct Tx {
    bool isFieldPresent(int) const;
    int operator[](int) const;
    int getFieldVL(int) const;
    int getAccountID(int) const;
    bool isFlag(unsigned) const;
};
struct Rules { bool enabled(int) const; };
int sfDelegate, sfSponsor, sfSigners, sfTicketSequence, sfIgnored;
unsigned tfInnerBatchTxn;
int featureBatchV1_1;
enum class FeePayerType { Account, Delegate, SponsorCoSigned, SponsorPreFunded };

int getFeePayer(Tx const& tx, Rules const& r) {
    if (tx.isFieldPresent(sfDelegate)) return 0;   // predicate idiom
    int a = tx[sfSponsor];                          // subscript idiom
    int b = tx.getAccountID(sfSponsor);             // getAccountID idiom
    int c = tx.getFieldVL(sfSigners);               // getFieldVL idiom
    if (tx.isFlag(tfInnerBatchTxn)) return 1;       // isFlag idiom
    if (r.enabled(featureBatchV1_1)) return 2;      // rules().enabled gate
    return a + b + c + sfIgnored;
}

int noFork(Tx const& tx) { return tx.isFieldPresent(sfIgnored); }
"""

# A function-local named after the amendment it caches. rippled writes these
# constantly (`bool const fixEnabled = ...`, `featureSAVEnabled`). Treated as a
# gate it would invent an edge and, since an unknown gate is a hard error, could
# fail the build outright.
LOCAL_SHADOW_FIXTURE = """
struct Rules { bool enabled(int) const; };
int sfDelegate;
int featureBatchV1_1;

int checkThing(Rules const& r, int sfDelegate_unused) {
    bool const fixEnabled = r.enabled(featureBatchV1_1);
    bool const featureSAVEnabled = r.enabled(featureBatchV1_1);
    return (fixEnabled || featureSAVEnabled) ? sfDelegate : 0;
}
"""

# Two unrelated free functions of the same name, in one namespace, in two files
# that are both in scan scope. Merging them would fabricate levers and edges
# between unrelated code.
COLLIDING_A = """
namespace xrpl {
int sfDelegate;
int doIt() { return sfDelegate; }
}
"""
COLLIDING_B = """
namespace xrpl {
int sfSponsor;
int doIt(int) { return sfSponsor; }
}
"""

# sfTicketSequence is common but unreferenced here; sfIgnored is referenced but
# not common -> lets us assert both the intersection filter and non-discovery.
COMMON = {"sfDelegate", "sfSponsor", "sfSigners", "sfTicketSequence"}


@pytest.fixture()
def fixture_forks(libclang_dylib, tmp_path):
    src = tmp_path / "fixture.cpp"
    src.write_text(FIXTURE)
    tu = ci.Index.create().parse(
        str(src), args=["-std=c++20"], unsaved_files=[(str(src), FIXTURE)]
    )
    return scan_translation_unit(tu, COMMON, str(src), str(tmp_path))


def test_levers_flags_gates_all_idioms(fixture_forks):
    # noFork references only sfIgnored (non-common) -> not discovered.
    assert [f.name for f in fixture_forks] == ["getFeePayer"]
    fork = fixture_forks[0]
    assert fork.lever_fields == {"sfDelegate", "sfSponsor", "sfSigners"}
    assert fork.lever_flags == {"tfInnerBatchTxn"}
    assert fork.gate_globals == {"featureBatchV1_1"}


def test_state_space_attached(fixture_forks):
    fork = fixture_forks[0]
    assert fork.state_space == [
        "Account",
        "Delegate",
        "SponsorCoSigned",
        "SponsorPreFunded",
    ]


def test_function_local_shadow_is_not_a_gate(libclang_dylib, tmp_path):
    src = tmp_path / "shadow.cpp"
    src.write_text(LOCAL_SHADOW_FIXTURE)
    tu = ci.Index.create().parse(
        str(src), args=["-std=c++20"], unsaved_files=[(str(src), LOCAL_SHADOW_FIXTURE)]
    )
    forks = scan_translation_unit(tu, {"sfDelegate"}, str(src), str(tmp_path))
    assert [f.name for f in forks] == ["checkThing"]
    # The globals survive; the locals named like levers do not.
    assert forks[0].gate_globals == {"featureBatchV1_1"}
    assert "fixEnabled" not in forks[0].gate_globals
    assert "featureSAVEnabled" not in forks[0].gate_globals


def test_same_name_functions_in_one_namespace_are_fatal(libclang_dylib, tmp_path):
    """The likeliest collision shape here: the scan spans Transactor.cpp plus
    ~19 helper headers, and several forks are namespace-scoped free functions."""
    helpers = tmp_path / "helpers"
    helpers.mkdir()
    header = helpers / "Other.h"
    header.write_text(COLLIDING_B)
    main = tmp_path / "main.cpp"
    source = COLLIDING_A + '#include "helpers/Other.h"\n'
    main.write_text(source)
    tu = ci.Index.create().parse(str(main), args=["-std=c++20", f"-I{tmp_path}"])
    with pytest.raises(ValueError, match="two unrelated definitions"):
        scan_translation_unit(
            tu, {"sfDelegate", "sfSponsor"}, str(main), str(tmp_path) + "/"
        )


# --- real build ------------------------------------------------------------


@pytest.fixture(scope="module")
def real_forks(request):
    build = None
    import build_graph

    build = build_graph.find_build_dir(REPO_ROOT)
    if build is None:
        pytest.skip("no build directory with compile_commands.json")
    if not Path("/opt/homebrew/opt/llvm/lib/libclang.dylib").exists():
        pytest.skip("libclang dylib not found")
    common = parse_common_fields(TXFORMATS)
    return {f.name: f for f in extract_forks(build, common)}


def test_real_getfeepayer(real_forks):
    fp = real_forks["getFeePayer"]
    assert {"sfSponsor", "sfSponsorSignature", "sfDelegate"} <= fp.lever_fields
    assert set(fp.state_space) == {
        "Account",
        "Delegate",
        "SponsorCoSigned",
        "SponsorPreFunded",
    }


def test_real_checkseqproxy(real_forks):
    sp = real_forks["checkSeqProxy"]
    assert "sfTicketSequence" in sp.lever_fields
    assert set(sp.state_space) == {"Seq", "Ticket"}


def test_real_checksign_gates(real_forks):
    cs = real_forks["checkSign"]
    assert {"sfSigners", "sfDelegate"} <= cs.lever_fields
    assert {"featureBatchV1_1", "featureLendingProtocol"} <= cs.gate_globals


def test_real_preflight2_flag(real_forks):
    pf = real_forks["preflight2"]
    assert "tfInnerBatchTxn" in pf.lever_flags


def test_real_autodiscovers_beyond_manual_set(real_forks):
    # calculateBaseFee branches on sponsorship but was absent from the old
    # hand-maintained list; auto-discovery must surface it (a recall win).
    assert "calculateBaseFee" in real_forks
    assert "sfSponsorSignature" in real_forks["calculateBaseFee"].lever_fields


def test_real_helper_header_fork_discovered(real_forks):
    # Exercises the /helpers/ branch of _in_scope: isFeeSponsored is defined in
    # include/xrpl/ledger/helpers/SponsorHelpers.h, not Transactor.cpp.
    assert "isFeeSponsored" in real_forks
    assert "sfSponsor" in real_forks["isFeeSponsored"].lever_fields


def test_real_fork_count_plausible(real_forks):
    # Sanity floor + upper bound: enough forks to be a real parse, not so many
    # that scope has blown out to unrelated functions.
    assert 16 <= len(real_forks) <= 40


def test_no_empty_forks(real_forks):
    for fork in real_forks.values():
        assert fork.lever_fields or fork.lever_flags or fork.gate_globals
