# `ConsensusProposal.h` — Consensus Position Record

## Role in the System

`ConsensusProposal` is the fundamental unit of communication in the XRPL Byzantine Fault Tolerant consensus protocol. During each consensus round, every participating validator must broadcast its view of two things: which set of transactions should be included in the next ledger, and when that ledger should close. This class encapsulates both, together with the metadata needed to authenticate the proposal, track its progression, and decide when to ignore it.

The class lives inside `src/xrpld/consensus/`, which houses the generic, adapter-pattern consensus engine. It is a header-only template, intentionally decoupled from concrete XRPL types. The real-world instantiation — `ConsensusProposal<NodeID, uint256, uint256>` — happens in `RCLCxPeerPos.h`, which wraps it with a cryptographic signature and public key for network propagation.

## Template Design

The three template parameters (`NodeID_t`, `LedgerID_t`, `Position_t`) mirror the broader pattern used throughout the consensus module, where `Consensus.h` defines `Proposal_t` as `ConsensusProposal<NodeID_t, typename Ledger_t::ID, typename TxSet_t::ID>`. This separation means the consensus logic is testable without pulling in the full XRPL type machinery: unit tests can instantiate `ConsensusProposal` over simple integer types while production code uses 256-bit hashes. The position type is the hash of a transaction set (`TxSet_t::ID`), not the set itself — proposals are compact identifiers, not the full data.

## The Sequence Number Protocol

The most architecturally significant design element is `proposeSeq_`, a monotonically increasing `uint32_t` used to order successive positions from the same peer within a single consensus round. Two sentinel values define lifecycle boundaries:

- `seqJoin` (0): The very first proposal a peer broadcasts when it joins a round (`isInitial()` returns true). The `ConsensusTypes.h` `ConsensusCloseTimes` struct specifically collects these initial close time estimates to measure inter-peer clock drift.
- `seqLeave` (0xffffffff): Signals that a peer is voluntarily exiting consensus via `bowOut()`. This is distinct from simply going silent; it lets the rest of the network immediately subtract that peer from the denominator when counting agreement.

`changePosition()` increments `proposeSeq_` each time a peer updates its stance, but explicitly guards against incrementing past `seqLeave`. This ensures a peer that has bowed out cannot accidentally re-enter by receiving an update call.

In `Consensus.h`, the engine checks `isBowOut()` before forwarding peer positions to dispute resolution (line 766), and similarly gates whether to re-broadcast the local node's own position (line 1591). The sequence number is also included in the signing hash, so replaying an older proposal with a forged higher sequence is not possible without breaking the signature.

## Lazy Signing Hash

`signingHash()` computes a `sha512Half` over `HashPrefix::proposal`, the sequence number, the close time epoch count, the previous ledger ID, and the position hash. This five-field digest is what peers sign with their private key and what verifiers check via `RCLCxPeerPos::checkSign()`.

The hash is stored as `mutable std::optional<uint256> signingHash_` and is computed only on first access. Both `changePosition()` and `bowOut()` explicitly call `signingHash_.reset()` before mutating any fields, invalidating the cached value. This lazy pattern avoids recomputing the digest on construction — which would be wasted work for proposals received from peers that are immediately re-signed and forwarded — while guaranteeing correctness: any mutation path must clear the cache first, or the accessor would return a stale hash.

## Staleness and the `time_` Field

`seenTime()` reflects when the position was last updated (either constructed or mutated via `changePosition`/`bowOut`). `isStale(cutoff)` compares `time_ <= cutoff` to detect peers that have gone silent. In `Consensus.h`, two cutoffs are computed — one for peer proposals (line 1437) and one for the local node's own position (line 1555) — and proposals older than their respective cutoff are discarded or replaced. This prevents the consensus engine from permanently blocking on a dead peer's stale proposal.

Notably, `time_` is the *wall-clock* time the proposal was seen locally, not the ledger close time. The ledger close time (`closeTime_`) is a separate field representing the proposing peer's estimate of when the ledger should close, using `NetClock` (the network's consensus clock). Conflating these two would be a subtle bug, so the naming distinction — `seenTime()` vs `closeTime()` — is deliberate.

## Relationship to `ConsensusResult`

`ConsensusTypes.h` defines `ConsensusResult<Traits>`, which holds a `Proposal_t position` field representing the *local node's* current position throughout the round. This is the one mutable `ConsensusProposal` in the system; all peer proposals are immutable once received. The `Consensus.h` engine mutates `result_->position` exclusively through `changePosition()` and `bowOut()`, making the sequence number and cache invalidation contract straightforward to audit.

## Equality and Debugging

The free `operator==` compares all six fields including `seenTime()`, making two proposals equal only if they were seen at the same instant. This is appropriate for de-duplication (the hash router suppression in `RCLCxPeerPos` handles network-level dedup separately), but callers should be aware that two logically identical positions received at different times will not compare equal. `render()` and `getJson()` provide human-readable diagnostics; `getJson()` omits `transaction_hash` and `propose_seq` when the proposal is a bow-out, which avoids logging a meaningless `0xffffffff` into the JSON output.