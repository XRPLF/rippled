# RCLCxPeerPos.h — Signed Peer Proposal for RCL Consensus

`RCLCxPeerPos` is the network-layer wrapper for a consensus proposal in the XRP Ledger. It takes the generic `ConsensusProposal` template from the protocol-layer consensus engine and binds it to XRPL's concrete types, then augments it with the cryptographic material — public key, signature, and suppression ID — required for secure peer-to-peer propagation.

## Role in the Consensus Stack

The XRPL consensus process is split into two layers. The generic `Consensus<>` engine in `src/xrpld/consensus/` operates on abstract types. The `RCL`-prefixed types in `src/xrpld/app/consensus/` are the concrete instantiations that talk to the live network. `RCLCxPeerPos` sits at the boundary: it is the object passed into `consensus_.peerProposal()` and the object serialized onto the wire by `RCLConsensus::Adaptor::share()`. Every proposal that a validator broadcasts or receives is an instance of this class.

The `Proposal` type alias concretizes the template parameters:

```cpp
using Proposal = ConsensusProposal<NodeID, uint256, uint256>;
```

`NodeID` identifies the originating validator node, the first `uint256` is the hash of the prior ledger the proposal builds upon, and the second `uint256` is the hash of the candidate transaction set being proposed.

## Signature Storage Design

The signature is stored as `boost::container::static_vector<std::uint8_t, 72>` rather than a `std::vector`. This is a deliberate performance choice: DER-encoded ECDSA/secp256k1 signatures have a well-known maximum size of 72 bytes. The `static_vector` lives entirely on the stack within the object itself, eliminating a heap allocation for every received proposal. The constructor guards this with both an `XRPL_ASSERT` and a runtime check before calling `assign`, ensuring a malformed oversized signature never causes undefined behaviour, even if the assertion is compiled out.

## Signing Hash and Verification

`checkSign()` delegates to `verifyDigest(publicKey(), proposal_.signingHash(), signature(), false)`. The `signingHash()` is computed lazily and cached inside `ConsensusProposal` via a `mutable std::optional<uint256>`. The hash is a `sha512Half` over the `HashPrefix::proposal` discriminator (`"PRP\0"`), the sequence number, close time, previous ledger hash, and the proposed transaction-set hash. Importantly, `changePosition()` and `bowOut()` reset `signingHash_` when fields change, keeping it consistent.

The private `hash_append()` method inside `RCLCxPeerPos` mirrors exactly those same fields. This makes the class participate in generic hashing algorithms using the `beast::hash_append` protocol, but it is separate from and should not be confused with the signing hash itself.

## Suppression ID and Duplicate Filtering

The `suppressionID()` is a `uint256` computed by the free function `proposalUniqueId()` at the point where the local node originally receives or creates a proposal. The function serializes all proposal fields — position, previous ledger, sequence, close time — together with the raw public key and signature bytes, then returns the SHA512-half of the concatenation:

```cpp
uint256 proposalUniqueId(
    uint256 const& proposeHash,
    uint256 const& previousLedger,
    std::uint32_t proposeSeq,
    NetClock::time_point closeTime,
    Slice const& publicKey,
    Slice const& signature);
```

This ID is passed directly to the hash router (`app_.getOverlay().relay(prop, peerPos.suppressionID(), ...)`) so that a proposal echoed back from a peer is recognized and dropped without re-processing. Incorporating the signature into the suppression ID means that even if two proposals had identical logical content, a corrupted or distinct signature would still produce a unique ID and not be incorrectly suppressed.

## The "Omitted Previous Ledger" Edge Case

The comment on `proposalUniqueId` documents an important protocol subtlety: the `previousLedger` field may legitimately be absent when a proposal is first transmitted. The signer computes `signingHash()` as if the field is present (using all-zeroes if absent), and recipients inject the actual last-closed-ledger value they know about before calling `checkSign()`. This tolerates the case where a node sends a proposal before it has confirmed which ledger is closing, while still allowing recipients to verify authorship once they have that context.

## Relationship to `RCLConsensus`

In `RCLConsensus::Adaptor::propose()`, when the local node crafts its own outbound proposal, it calls `proposalUniqueId()` to build the suppression ID before constructing the `RCLCxPeerPos`. In `RCLConsensus::peerProposal()`, inbound proposals arrive already wrapped as `RCLCxPeerPos` objects (constructed upstream in the overlay/peer message handler) and are forwarded straight into the consensus engine. `getJson()` adds a `peer_id` field (the Base58-encoded node public key) on top of `ConsensusProposal::getJson()`, which is used for RPC introspection and logging.