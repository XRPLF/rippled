"""Common-field set and Privilege enum extraction.

Expected counts are re-derived from the source with independent regexes rather
than hardcoded.
"""

import re
from pathlib import Path

from common_fields import parse_common_fields
from privilege_extractor import parse_privileges

REPO_ROOT = Path(__file__).resolve().parents[3]
TXFORMATS = REPO_ROOT / "src/libxrpl/protocol/TxFormats.cpp"
PRIVILEGE_HEADER = REPO_ROOT / "include/xrpl/tx/invariants/InvariantCheckPrivilege.h"


def _expected_common_fields() -> int:
    block = TXFORMATS.read_text().split("kCommonFields", 1)[1].split("};", 1)[0]
    return len(re.findall(r"\{\s*sf\w+", block))


def _expected_privilege_bits() -> int:
    block = PRIVILEGE_HEADER.read_text().split("enum Privilege", 1)[1].split("};", 1)[0]
    names = re.findall(r"(\w+)\s*=\s*0x[0-9A-Fa-f]+", block)
    return len([n for n in names if n != "NoPriv"])


def test_common_fields():
    fields = parse_common_fields(TXFORMATS)
    assert len(fields) == _expected_common_fields()
    # The governing levers must be present so forks branching on them register.
    for lever in (
        "sfDelegate",
        "sfSponsor",
        "sfSponsorSignature",
        "sfTicketSequence",
        "sfSigners",
        "sfNetworkID",
    ):
        assert lever in fields


def test_privilege_bits():
    bits = parse_privileges(PRIVILEGE_HEADER)
    assert "NoPriv" not in bits  # the zero sentinel is excluded
    assert len(bits) == _expected_privilege_bits()
    assert "CreateAcct" in bits
    assert "MayCreateMpt" in bits
