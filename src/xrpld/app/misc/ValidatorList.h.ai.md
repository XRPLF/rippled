# `ValidatorList.h` — Trusted Validator (UNL) Management

`ValidatorList` is the central authority for determining which validator nodes this XRPL node will trust for ledger validation. It manages the entire lifecycle of the Unique Node List (UNL): loading initial configuration, applying publisher-signed updates, rotating future-dated lists into active use, computing quorum values, and broadcasting new lists to peers. Nothing about consensus trust lives outside this class and its companion `.cpp`.

## Why This Design Exists

XRPL consensus requires each node to maintain a set of validator public keys it will accept validations from. A ledger is considered fully validated once the node receives signed validations from a quorum of its trusted set. Rather than hard-coding these keys, the protocol supports *publisher-signed lists*: well-known organizations publish signed JSON blobs listing trusted validator keys. This lets the trusted set evolve without requiring every node operator to manually reconfigure their node for each change.

The dual-trust model — trust publishers to sign lists, trust lists to name validators — means a compromise of a publisher's *signing* key only requires the publisher to rotate to a new ephemeral key (via a manifest), not reconfigure every node. Similarly, validator operators can rotate their signing keys by issuing new manifests signed with their master key. `ValidatorList` maintains both a `ManifestCache& validatorManifests_` (for validators) and `ManifestCache& publisherManifests_` (for publishers), and understands that a `PublicKey` arriving in a validation message may be an ephemeral signing key that must be resolved back to a master key.

## Internal Data Hierarchy

The nesting of data structures reflects the protocol's version evolution.

`PublisherList` holds the data for one specific version of a publisher's list: the sorted vector of validator master `PublicKey`s, the raw encoded blob, signature, optional per-blob manifest, sequence number, and validity window (`validFrom` / `validUntil`).

`PublisherListCollection` groups everything from a single publisher. It contains:
- `current` — the active `PublisherList` (highest sequence whose effective date has arrived)
- `remaining` — a `std::map<std::size_t, PublisherList>` of future-dated lists indexed by sequence number, sorted ascending
- `maxSequence` — the highest sequence number ever seen from this publisher
- `fullHash` — a precomputed hash over the entire collection for quick duplicate detection

The semantic rule documented inline is important: once a list with sequence *N* becomes current, all lists with sequences below *N* are permanently obsolete — there is no rollback to a previous list, even if the current one expires. This prevents an attacker from replaying an old list to reactivate removed validators.

The global `publisherLists_` hash map from `PublicKey` → `PublisherListCollection` is the master data store. A separate `keyListings_` hash map counts how many publisher lists each validator master key currently appears on — this reference count is what enables the `listThreshold_` policy.

## List Application Pipeline

Incoming lists arrive through `applyListsAndBroadcast()`, which delegates the actual state update to `applyLists()`, which in turn calls the private `applyList()` once per blob. The work inside `applyList()` follows a strict order: deserialize and `verify()` the manifest, check signature and JSON structure, classify the sequence relative to what's known, then update `current` or `remaining`. After all blobs are processed, `applyLists()` purges any `remaining` entries made redundant (sequence ≤ current, or a later-sequenced entry with an earlier or equal effective date that would be skipped over anyway). The collection is then persisted via `cacheValidatorFile()` and its `fullHash` is recomputed.

The `verify()` private method enforces the full disposition lattice: an unrecognized publisher master key returns `untrusted`; a revoked manifest returns `untrusted` and immediately calls `removePublisherList()`; a failed cryptographic signature returns `invalid`; an already-current sequence returns `same_sequence`; a sequence below current returns `stale`; an already-known future sequence returns `known_sequence`; an expired list (past its `validUntil`) still advances the publisher's sequence number, returning `expired`; a future-dated list returns `pending`. Only `accepted` and `expired` sequences cause `keyListings_` to be updated.

Both `ListDisposition` and `PublisherStatus` enums are explicitly ordered by desirability (lower integer = better), which allows `PublisherListStats::bestDisposition()` and `worstDisposition()` to cheaply summarize the result of processing a multi-blob V2 collection.

## Trust Computation: `updateTrusted()`

`updateTrusted()` is the heartbeat of trust management, called at the start of each consensus round. Under an exclusive lock it performs four sequential tasks:

1. **List rotation**: For each publisher collection, if any `remaining` entry has a `validFrom` ≤ `closeTime`, the highest such entry is moved into `current`. Entries skipped over are erased. This triggers `updatePublisherList()` to diff the old and new validator sets and adjust `keyListings_` counts, then `broadcastBlobs()` to propagate the newly activated list to peers.

2. **Expiry handling**: If a publisher's `current` list has passed its `validUntil`, `removePublisherList()` clears all its validators from `keyListings_` and marks the publisher `expired`. `NetworkOPs::setUNLBlocked()` is called to signal that ledger validation is degraded.

3. **Trust set rebuild**: `trustedMasterKeys_` is rebuilt from scratch based on `keyListings_` counts vs. `listThreshold_`, excluding any revoked manifests. If any keys changed, `trustedSigningKeys_` is also rebuilt by resolving each trusted master key to its current ephemeral signing key via `validatorManifests_`.

4. **Quorum calculation**: `calculateQuorum()` computes the new quorum. The baseline formula is `max(ceil(effectiveUnlSize × 0.8), ceil(unlSize × 0.6))`, derived from Theorem 8 of the XRP LCP Analysis paper (arXiv:1802.07242). The 0.6 floor comes from the Negative UNL protocol's `AbsoluteMinimumQuorum`. If too many publishers are unavailable (exceeding `min(listThreshold_, N - listThreshold_ + 1)` where N is total publishers), quorum is set to `std::numeric_limits<std::size_t>::max()` — effectively making validation impossible until lists are recovered. The `quorum_` field is `std::atomic<std::size_t>` so it can be read without acquiring the lock.

## The `listThreshold_` Policy

`listThreshold_` controls how many publisher lists a validator must appear on to be trusted. When `load()` is called without an explicit threshold, it defaults to 1 for fewer than three publishers, or to `(N/2) + 1` for N ≥ 3 publishers — a majority rule. This prevents a single compromised publisher from unilaterally adding malicious validators to the trusted set. The local node's own key is always inserted into `keyListings_` with a count of exactly `listThreshold_` so the node never excludes itself.

## Concurrency Architecture

`ValidatorList` uses a `shared_mutex` (`boost::thread::shared_mutex` aliased to the standard type) for reader-writer separation. The private overloads of `trusted()`, `getTrustedKey()`, `expires()`, and `count()` accept an already-held lock object to avoid double-locking when callers need to hold the lock across multiple reads. This "lock-passing" idiom also documents locking contracts at the type level: a `lock_guard const&` parameter signals that the caller must hold an exclusive lock; `shared_lock const&` signals a read lock. The `applyList()` private method requires an exclusive `lock_guard` even though it may only be reading, because it can mutate `publisherLists_` — the locking contract makes this clear.

## Network Propagation

`broadcastBlobs()` uses `HashRouter::shouldRelay()` to determine which peers have not yet seen a given list hash, then calls `sendValidatorList()` per peer. Peers are checked for protocol feature support: `ValidatorList2Propagation` (V2) peers receive all blobs with sequence numbers greater than what they've already seen, batched into `TMValidatorListCollection` messages that are split by binary recursion if they exceed `maximumMessageSize`. V1-only peers receive only the `current` blob as a `TMValidatorList` message. The `MessageWithHash` struct caches already-built `Message` objects so that if multiple V2 peers have the same known sequence number, the protobuf serialization is done only once.

## File Caching and Bootstrap

`cacheValidatorFile()` writes each publisher's full collection to `dataPath_ / "cache.<pubKeyHex>"` as a styled JSON file. On startup, if URL fetching fails, `loadLists()` scans the publisher map for any that are still `unavailable` and returns `file://` URIs pointing to cached files. `ValidatorSites` (not shown here) then treats these as normal HTTP sources and re-applies them, providing offline bootstrap when the internet-hosted list is temporarily unreachable.

## Negative UNL Integration

`negativeUNL_` holds master keys of validators that have been voted into the negative UNL by consensus — validators that have been unreliable recently. `setNegativeUNL()` is called when a new ledger is validated to sync this set. `negativeUNLFilter()` removes validations from these validators before they're counted toward quorum in the consensus engine. Crucially, the quorum formula accounts for the negative UNL by computing `effectiveUnlSize = unlSize - |negativeUNL_ ∩ trustedMasterKeys_|`, lowering the quorum while keeping the 60% floor based on the original `unlSize`.