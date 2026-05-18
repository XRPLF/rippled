# `LedgerTiming.h` — Ledger Close Time Resolution and Binning

This header provides the three free functions and set of compile-time constants that govern how XRPL records and agrees upon ledger close times. It sits at the intersection of consensus and ledger construction: every time a new ledger is accepted, these utilities translate a raw wall-clock observation into a canonical, network-agreed timestamp that is written into the immutable ledger record.

## The Problem: Agreeing on Time Without Synchronized Clocks

XRPL validators run on independent machines with imperfectly synchronized clocks. If each validator stamped a ledger with its own wall-clock reading, the resulting close times would differ slightly across the network, making it impossible to form consensus on a single ledger hash. The solution — also used in several other distributed systems — is **time binning**: rather than recording the exact close time, each validator rounds its observation to the nearest multiple of a fixed resolution, then votes on that rounded value. When the resolution is large enough, minor clock skew disappears and all validators naturally agree.

The challenge becomes choosing the right bin size. Too coarse and ledger close times carry less useful information; too fine and even small clock differences cause disagreements. `LedgerTiming.h` implements an adaptive mechanism that adjusts the bin size dynamically based on whether the network agreed on close time in the previous round.

## Constants and Their Relationships

`ledgerPossibleTimeResolutions` is a constexpr array of six candidate resolutions: 10, 20, 30, 60, 90, and 120 seconds. They form a strictly increasing sequence that `getNextLedgerTimeResolution` traverses. The default resolution for ordinary ledgers is 30 seconds (index 2), while the genesis ledger starts at 10 seconds (index 0). Storing these as a flat array rather than a set or map is deliberate: membership checks and boundary navigation are O(n) over only six elements, and the array order directly encodes the "coarser/finer" direction needed by the adjustment logic.

Two additional constants control the pace of change: `decreaseLedgerTimeResolutionEvery = 1` (react on every ledger) and `increaseLedgerTimeResolutionEvery = 8` (only increase resolution every eighth ledger). This asymmetry is intentional and conservative — when the network is disagreeing it needs to back off to a coarser bin quickly, but it should be cautious about tightening the resolution, since a premature increase could immediately cause fresh disagreements.

## `getNextLedgerTimeResolution()` — Adaptive Resolution Selection

This function takes the previous ledger's resolution, whether consensus agreed on that ledger's close time, and the new ledger's sequence number. It applies the two competing adjustment rules:

- If the prior ledger saw **disagreement** and `ledgerSeq % decreaseLedgerTimeResolutionEvery == 0`, the resolution increases (moves toward coarser bins, i.e., toward the end of `ledgerPossibleTimeResolutions`).
- If the prior ledger saw **agreement** and `ledgerSeq % increaseLedgerTimeResolutionEvery == 0`, the resolution decreases (moves toward finer bins, i.e., toward the beginning).

Crucially, neither rule fires if the iterator is already at the boundary, so the resolution saturates at the min (10 s) or max (120 s) rather than wrapping or asserting. The sequence-number modulo check ensures both conditions cannot simultaneously trigger on the same ledger even if `ledgerSeq` happens to be divisible by both constants (which can't happen given the values are 1 and 8, but the guard is still logically sound). The function is a header-only template parameterized on both the `std::chrono::duration` type and the ledger sequence type, allowing it to work with both built-in integers and XRPL's `tagged_integer` wrappers without casts.

The `Consensus.h` engine calls this function at the start of every consensus round to compute `closeResolution_`, storing the result for the rest of that round's voting and for embedding in the accepted ledger:

```cpp
closeResolution_ = getNextLedgerTimeResolution(
    previousLedger_.closeTimeResolution(),
    previousLedger_.closeAgree(),
    previousLedger_.seq() + typename Ledger_t::Seq{1});
```

## `roundCloseTime()` — Epoch-Anchored Binning

This function rounds an arbitrary `time_point` to the nearest multiple of `closeResolution`. The rounding arithmetic is `(closeTime + resolution/2) - ((closeTime + resolution/2).time_since_epoch() % resolution)` — effectively a floor-after-offset operation. Two properties deserve attention:

**Epoch anchoring.** The modulo is applied to `time_since_epoch()`, not to a relative offset from some local reference. This means bins are aligned to absolute epoch-relative boundaries (multiples of 30 s from the XRPL epoch), not to whenever this particular ledger happened to open. Any two validators computing this on the same raw time will produce the same bin, regardless of when they run the calculation — a correctness prerequisite.

**Tie-breaking upward.** Adding half the resolution before truncating means a time exactly at the midpoint rounds up to the next bin, matching the standard "round half up" convention. The unit tests in `LedgerTiming_test.cpp` confirm this: `roundCloseTime(tp{30s}, 60s)` returns `tp{60s}`, not `tp{0s}`.

**Zero sentinel.** A `time_point{}` (the epoch itself) is returned unchanged. The zero value is a protocol-level sentinel meaning the ledger has no agreed close time — used when consensus failed to agree — and binning must not accidentally produce a plausible timestamp from it.

`roundCloseTime` is called internally by `effCloseTime` and also exposed so the consensus engine can canonicalize individual peer proposals via the `asCloseTime()` helper in `Consensus.h`.

## `effCloseTime()` — Monotonicity Enforcement

After rounding, a subtle edge case remains: if a ledger closes very quickly after its predecessor, `roundCloseTime` might produce a result equal to or earlier than the prior ledger's close time. This would violate the invariant that close times increase monotonically along the ledger chain, which downstream consumers (auditing, ordering, client-visible timestamps) rely on.

`effCloseTime` resolves this with a single `std::max`: `max(roundCloseTime(closeTime, resolution), priorCloseTime + 1s)`. The `+ 1s` ensures strict ordering rather than merely ≥. When the rounded time is later than the prior close time the rounding result passes through unchanged; when it would tie or go backward, the function returns `priorCloseTime + 1s` instead. The consensus simulation framework (`src/test/csf/impl/ledgers.cpp`) and the real consensus adapter (`RCLConsensus.cpp`) both call `effCloseTime` at ledger acceptance time to compute the final value written into the ledger.

The test for this function (`testEffCloseTime`) exercises the intersection point: `effCloseTime(tp{10s}, 30s, tp{0s})` returns `tp{1s}` because rounding 10 s to 30 s bins gives `tp{0s}`, which is not greater than `priorCloseTime + 1s = tp{1s}`, so the minimum applies. Meanwhile `effCloseTime(tp{16s}, 30s, tp{0s})` gives `tp{30s}` because the rounded value wins.

## Design Summary

The file is deliberately narrow: it encapsulates precisely the time-agreement logic that must be identical on every validator. Being a header-only template library means the logic is linked into any translation unit that needs it without a separate compilation dependency, and the template parameters allow the functions to operate on XRPL's network clock type (`NetClock`) without hardcoding it. The three constants controlling the adjustment rate (`decreaseLedgerTimeResolutionEvery`, `increaseLedgerTimeResolutionEvery`, and the resolution ladder itself) are the primary knobs for tuning network time-agreement behavior, and their values reflect a deliberate bias toward stability over precision.