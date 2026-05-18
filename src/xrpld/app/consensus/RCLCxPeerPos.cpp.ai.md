# `RCLCxPeerPos.cpp` — Signed Peer Proposal for RCL Consensus

This file implements `RCLCxPeerPos`, the XRPL network's concrete representation of a validator's signed position during a consensus round. It is the glue layer between the generic consensus engine (`ConsensusProposal`) and the network transport layer — wrapping a proposal with its cryptographic proof of origin and its hash-router suppression identity.

## Role in the Consensus Pipeline

XRPL's consensus protocol is built around a generic `ConsensusProposal<NodeID, LedgerID, Position>` template. That template knows nothing about signatures, public keys, or network deduplication. `RCLCxPeerPos` layers exactly those concerns on top: it owns a `PublicKey`, a signature, a pre-computed suppression hash (`suppression_`), and the concrete proposal instantiated as `ConsensusProposal<NodeID, uint256, uint256>`. This separation lets the consensus algorithm remain topology-agnostic while the network layer retains full control over trust and replay suppression.

## Constructor and Signature Storage

The constructor is called exclusively for *received* proposals — when a peer's `TMProposeSet` message is unpacked by `PeerImp`. The signature arrives as a raw `Slice` and must be stored durably. The design choice here is deliberate: `signature_` is declared as `boost::container::static_vector<std::uint8_t, 72>`. This is a fixed-capacity container allocated entirely on the stack/inline with no heap allocation, whose capacity exactly matches the maximum DER-encoded ECDSA signature size (72 bytes).

The constructor enforces this limit with two independent checks:

```cpp
XRPL_ASSERT(
    !signature.empty() && signature.size() <= signature_.capacity(),
    "xrpl::RCLCxPeerPos::RCLCxPeerPos : valid signature size");

if (!signature.empty() && signature.size() <= signature_.capacity())
    signature_.assign(signature.begin(), signature.end());
```

The `XRPL_ASSERT` fires in debug builds (typically aborting), catching protocol violations during development. The `if` guard runs in all builds — if an oversized signature somehow slips through validation upstream, the assignment is silently skipped rather than overflowing the static buffer. This dual pattern is common in the XRPL codebase: assert to detect bugs early, guard to preserve invariants in production.

## Signature Verification: `checkSign()`

`checkSign()` delegates entirely to `verifyDigest()`, passing the stored public key, the proposal's signing hash (computed by `ConsensusProposal`), and the stored signature. It returns a `bool` rather than throwing; callers in `PeerImp` discard proposals that fail this check. This function is the point at which a received proposal transitions from "structurally valid" to "cryptographically authenticated."

## `proposalUniqueId()` — The Suppression Hash

The free function `proposalUniqueId()` is called in two places: `RCLConsensus::propose()` when the local validator broadcasts its own position, and `PeerImp` when a peer proposal first arrives. In both cases the result seeds the hash router to deduplicate proposal flooding.

```cpp
Serializer s(512);
s.addBitString(proposeHash);
s.addBitString(previousLedger);
s.add32(proposeSeq);
s.add32(closeTime.time_since_epoch().count());
s.addVL(publicKey);
s.addVL(signature);
return s.getSHA512Half();
```

The function commits every semantically distinguishing field — position hash, previous ledger, sequence number, close time, public key, and the signature itself — into a single `SHA512Half` digest. Including the signature in this ID is a considered choice: it makes the suppression ID unique not just per *proposal content* but per *signed emission*. Two validators could in theory agree on identical proposal fields, yet their differing signatures produce distinct suppression IDs, so their broadcasts are tracked independently by the relay mechanism. It also guarantees that a verbatim replayed message (same bytes, same signature) produces the same suppression ID and is silently dropped, even if it arrives via a different peer.

In `PeerImp`, immediately after calling `proposalUniqueId()`, the result is passed to `app_.getHashRouter().addSuppressionPeerWithStatus()`. If that returns `!added`, the proposal is a duplicate and is dropped before any cryptographic verification occurs — ensuring expensive `checkSign()` calls are never made on redundant traffic.

## JSON Rendering

`getJson()` delegates to `proposal().getJson()` for the proposal fields and then conditionally appends `peer_id` as a Base58-encoded node public key. The size guard on `publicKey()` before calling `toBase58()` handles the edge case where a locally-originated proposal (which has no remote peer key) is serialized — in that path the `peer_id` field is simply omitted.

## Relationship to Sibling Files

Within `src/xrpld/app/consensus/`, `RCLCxPeerPos` is the peer-proposal counterpart to `RCLCxLedger` (ledger wrapper) and `RCLCxTx` (transaction wrapper) — all three adapt generic consensus types to the concrete XRPL protocol. `RCLConsensus.cpp` is the primary consumer, constructing `RCLCxPeerPos` objects from inbound wire messages and passing them upward into the generic consensus algorithm.