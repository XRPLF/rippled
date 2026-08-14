"""One deterministic ordering for every bounded interaction-review consumer."""

from __future__ import annotations

from typing import Any

TIER_RANK = {"review": 0, "consider": 1}


def budget_sort_key(row: dict[str, Any]) -> tuple[int, int]:
    """Review before consider, then exact descending score; callers stay stable."""
    return TIER_RANK.get(row["tier"], 2), -row["score"]
