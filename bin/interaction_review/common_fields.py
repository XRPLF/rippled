#!/usr/bin/env python3
"""Parse the cross-cutting (common) SField set from TxFormats.cpp.

These are the fields every transaction may carry (TxFormats::getCommonFields).
A branch in a base-pipeline fork counts as a feature lever only when it tests
one of these fields, so this set is the fork extractor's relevance filter.
"""

from __future__ import annotations

import re
from pathlib import Path

# Isolate the kCommonFields initializer list, then pull each {sfName, ...} entry.
_BLOCK_RE = re.compile(
    r"kCommonFields\s*=\s*std::vector<SOElement>\{(.*?)\};", re.DOTALL
)
_FIELD_RE = re.compile(r"\{\s*(sf\w+)\s*,")


def parse_common_fields(txformats_path: Path) -> set[str]:
    text = Path(txformats_path).read_text()
    match = _BLOCK_RE.search(text)
    if not match:
        raise ValueError(f"kCommonFields initializer not found in {txformats_path}")
    fields = set(_FIELD_RE.findall(match.group(1)))
    if not fields:
        raise ValueError(f"No common fields parsed from {txformats_path}")
    return fields
