# ValidatorList.cpp

## Role in the System

This file implements `ValidatorList`, the authority on which validators an XRPL node trusts for consensus. The XRP Ledger achieves finality when a quorum of trusted validators agree on the same ledger; this class answers at every step: *which validators are trusted?*, *what quorum is required?*, and *has a validator's publisher list changed?*

Two independent trust sources coexist inside the class. Static keys loaded from the local config (`[validators]`) are stored in `localPublisherList` — they never expire and carry no publisher identity. Dynamically-signed lists downloaded from network publishers are stored in `publisherLists_`, keyed by the publisher's master `PublicKey`. The class merges both sources into a single authoritative set of `trustedMasterKeys_` and a parallel set of `trustedSigningKeys_` (ephemeral keys derived via manifests).

---

## Key Data Structures

**`PublisherListCollection`** (private) holds everything known about a single publisher. It separates `current` (the VL currently in effect — the highest sequence that has ever become active) from `remaining` (future VLs whose `validFrom` is still in the future). This split exists because the v2 format allows publishers to pre-publish successive lists before the current one expires, creating a chain of overlapping validity windows. `remaining` is a `std::map<std::size_t, PublisherList>` ordered by sequence.

**`keyListings_`** is a `hash_map<PublicKey, std::size_t>` counting how many publisher lists each validator master key appears on. A validator is only eligible for trust if this count meets `listThreshold_`. The count is incrementally maintained by `updatePublisherList()` using a two-pointer merge-walk over sorted `list` vectors — O(n) rather than two O(n log n) set differences.

**`ListDisposition`** is an enum whose integer ordering is intentional and load-bearing. Lower values are "better" (`accepted = 0`, `expired`, `pending`, …, `invalid`). `PublisherListStats` wraps a `std::map<ListDisposition, std::size_t>` counting occurrences, and `bestDisposition()` / `worstDisposition()` simply return `begin()->first` / `rbegin()->first` — the ordering of the map mirrors the ordering of the enum.

---

## Initialization Path

`load()` runs at startup and accepts config keys, publisher keys, and an optional `listThreshold`. Publisher keys are hex-decoded and validated with `publicKeyType()`; any key whose manifest has already been revoked is marked `PublisherStatus::revoked` immediately. The local node's own signing key, if configured, is resolved to its master key via `validatorManifests_.getMasterKey()` and inserted into `keyListings_` with a count equal to `listThreshold_`, preventing the node from ignoring itself.

`listThreshold_` defaults to 1 for fewer than three publishers, and `(N/2)+1` for three or more — a simple majority requirement. A manually-specified threshold bypasses this formula (the config class enforces range).

The `minimumQuorum_` parameter (from `--quorum` on the command line) overrides all quorum calculation. Its presence triggers a `JLOG` warning at every quorum computation to signal that the node is operating in potentially unsafe mode.

---

## Ingest Path: `applyList` → `applyLists` → `applyListsAndBroadcast`

`applyLists()` is the entry point for a complete VL payload — it loops over all `ValidatorBlobInfo` entries in the payload and calls `applyList()` on each. After the loop, it performs a cleanup pass on `remaining`: any entry whose sequence is ≤ `current.sequence` or whose `validFrom` is not strictly later than the next entry's `validFrom` is pruned. This enforces the invariant that `remaining` contains only strictly increasing, non-redundant future VLs.

`applyList()` delegates cryptographic verification to `verify()`, which:
1. Rejects keys not in `publisherLists_` as `untrusted`.
2. Applies the manifest through `publisherManifests_`, atomically handling revocation — a revoked manifest clears `remaining` and removes the current list before returning `untrusted`.
3. Verifies the blob signature using the publisher's ephemeral signing key.
4. Parses and validates JSON fields (`sequence`, `expiration`, optionally `effective`, `validators`).
5. Classifies the result: `expired` if `validUntil ≤ now`, `pending` if `validFrom > now`, `stale` if sequence is older than `current.sequence`, `same_sequence`, `known_sequence`, or `accepted`.

Both `accepted` and `expired` advance the `current` slot. An `expired` list is stored because it represents the most recent authoritative VL from that publisher even though its time window has passed — it cannot be superseded by an older sequence.

After `applyLists()` completes, `cacheValidatorFile()` serializes the full `PublisherListCollection` to a `cache.<hex_pubkey>` file on disk. On the next restart, `loadLists()` returns `file://` URIs for these files, which `ValidatorSite` fetches as if they were remote URLs, bootstrapping list state without waiting for network refetches.

---

## Propagation Machinery

The propagation layer is version-aware:

- Peers advertising `ValidatorListPropagation` receive v1 messages (`TMValidatorList`, single blob).
- Peers advertising `ValidatorList2Propagation` receive v2 messages (`TMValidatorListCollection`, multiple blobs) enabling delivery of the full pending chain in one round-trip.

`broadcastBlobs()` consults `hashRouter.shouldRelay()`, then iterates `overlay.getActivePeers()`. For each eligible peer it only sends VLs with higher sequence numbers than what the peer already has. V2 messages are grouped by `peerSequence` so the same pre-built `MessageWithHash` is reused across peers with the same known sequence.

`buildValidatorListMessages()` lazily constructs messages: the first call builds and caches the protobuf; subsequent calls reuse it. If a `TMValidatorListCollection` exceeds `maximumMessageSize`, `splitMessage()` recursively bisects the blob list until each fragment fits, potentially downgrading to individual `TMValidatorList` messages when a partition reaches size 1.

---

## Trust Update Cycle: `updateTrusted`

Called at the start of each consensus round while holding an exclusive write lock.

**Pending rotation.** For each publisher collection, if the first entry in `remaining` has `validFrom ≤ closeTime`, `updateTrusted` scans forward to find the *last* candidate that is ready to go live, moves it into `current`, calls `updatePublisherList()` to reconcile `keyListings_`, and erases all consumed entries from `remaining` in one `erase(first, next(iter))`.

**Expiry.** If `current.validUntil ≤ closeTime`, `removePublisherList()` decrements `keyListings_` for every validator in that publisher's list, clears the list, and sets status to `expired`. `ops.setUNLBlocked()` signals that consensus is degraded.

**Trust set reconstruction.** The method sweeps `trustedMasterKeys_` removing keys whose listing count dropped below `listThreshold_` or whose manifest was revoked, then sweeps `keyListings_` to add newly eligible keys. If either set changes, `trustedSigningKeys_` is rebuilt from scratch.

**Quorum calculation.** `calculateQuorum()` uses 80% of the effective UNL size (UNL minus Negative UNL validators), floored at 60% of the full UNL size (the `AbsoluteMinimumQuorum` from the Negative UNL protocol). Before reaching the formula it checks whether unavailable publishers exceed `errorThreshold` — the minimum of `listThreshold_` and `N - listThreshold_ + 1`. If so, quorum is set to `std::numeric_limits<std::size_t>::max()`, making consensus impossible until lists are restored. This is the primary liveness-safety tradeoff: the node prefers to stall rather than reach quorum with an incomplete trust picture.

---

## Concurrency Model

`mutex_` is a `std::shared_mutex`. Read-heavy queries (`listed()`, `trusted()`, `getTrustedKey()`, `expires()`, `getJson()`, `for_each_listed()`) acquire `std::shared_lock`. Write paths (`applyLists()`, `updateTrusted()`, `load()`, `setNegativeUNL()`) acquire `std::lock_guard`. Several methods are overloaded with a lock-token parameter (e.g., `trusted(shared_lock const&, …)`) so callers that have already acquired the lock can call them without re-locking — a pattern used heavily inside `updateTrusted()` and `getJson()`.

`quorum_` is `std::atomic<std::size_t>`, allowing the consensus engine to read it without acquiring the mutex; it is only written from `updateTrusted()` while holding the exclusive lock.

---

## Negative UNL Integration

`negativeUNL_` is a `hash_set<PublicKey>` set externally via `setNegativeUNL()`. In `updateTrusted()`, validators in this set are subtracted from `effectiveUnlSize`, lowering the quorum threshold gracefully during periods of validator downtime. `negativeUNLFilter()` provides a post-hoc filter on raw validation messages, removing validations from negative-UNL validators before they reach the consensus engine.

---

## Design Decisions Worth Noting

The `current`/`remaining` split avoids any ambiguity about which list is "live" — the invariant is enforced at multiple points (`applyLists` cleanup, `updateTrusted` rotation) rather than computed on demand, keeping trust queries as O(1) lookups into pre-computed sets.

The `keyListings_` reference count (rather than per-publisher membership sets) means "is this key trusted?" is a single hash lookup, not a scan across all publisher lists. The merge-walk in `updatePublisherList()` works because `PublisherList::list` is always sorted (enforced by `std::sort` at the end of `applyList`'s list-building loop).

The `buildFileData()` static method serializes a `PublisherListCollection` back to JSON — the inverse of `parseBlobs()`. A `forceVersion` parameter allows the `/vl/<key>` HTTP endpoint to serve a v1-format response to clients that do not support v2, downgrading a multi-blob collection to its current blob only.