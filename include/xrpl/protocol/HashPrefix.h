/** @file
 *  Protocol hash domain separation via 4-byte prefixes.
 *
 *  Every XRPL hashing context prepends a `HashPrefix` constant to its input
 *  so that two structurally different objects that share identical serialized
 *  bytes can never collide in hash space. See `HashPrefix` for the full list
 *  of contexts and `hash_append` for the N3980-compatible integration point.
 */

#pragma once

#include <xrpl/beast/hash/hash_append.h>

#include <cstdint>

namespace xrpl {

namespace detail {

/** Pack three ASCII characters into the high 24 bits of a `uint32_t`.
 *
 *  The resulting value has the form `(a << 24) | (b << 16) | (c << 8)`,
 *  leaving the low byte as zero. The trailing zero acts as an implicit
 *  separator and prevents any prefix from coinciding with a valid 1- or
 *  2-byte byte sequence. The ASCII mnemonics make prefixes self-documenting
 *  in hex dumps (e.g. `TransactionId` appears as `0x54584E00`, i.e. `TXN\0`).
 *
 *  @param a First character of the 3-letter mnemonic.
 *  @param b Second character of the 3-letter mnemonic.
 *  @param c Third character of the 3-letter mnemonic.
 *  @return A `constexpr` `uint32_t` suitable for use as a `HashPrefix` value.
 */
constexpr std::uint32_t
makeHashPrefix(char a, char b, char c)
{
    return (static_cast<std::uint32_t>(a) << 24) + (static_cast<std::uint32_t>(b) << 16) +
        (static_cast<std::uint32_t>(c) << 8);
}

}  // namespace detail

/** 4-byte domain-separation sentinels prepended to every XRPL hash input.
 *
 *  Each enumerator identifies a distinct hashing context. Prepending the
 *  prefix ensures that two objects from different contexts with byte-for-byte
 *  identical serializations always produce different digests, closing a class
 *  of hash-collision attacks at the protocol layer.
 *
 *  The prefix is consumed in one of two ways depending on the call site:
 *  - **Serializer prefix** (`s.add32(HashPrefix::TxSign)`): writes the raw
 *    `uint32_t` into a `Serializer` buffer before appending signing fields.
 *  - **`hash_append` composition** (`hash_append(h, HashPrefix::InnerNode)`):
 *    feeds the 4-byte value directly into a streaming hasher, avoiding an
 *    intermediate buffer.
 *
 *  Both produce the same 4-byte prefix at position zero of the hash input.
 *
 *  @note Hash prefixes are protocol-immutable. Changing the mnemonic letters
 *      or the numeric value of any enumerator breaks consensus and cross-node
 *      compatibility irreversibly.
 */
enum class HashPrefix : std::uint32_t {
    /** Canonical transaction ID: SHA-512/2 of `TXN\0` followed by the
     *  transaction bytes including its signature field.
     *  Distinct from `TxSign` (which excludes the signature) so that signing
     *  payloads and transaction IDs operate in separate hash namespaces.
     */
    TransactionId = detail::makeHashPrefix('T', 'X', 'N'),

    /** Transaction-plus-metadata leaf node in the transaction SHAMap
     *  (`SND\0`). Used by `SHAMapTxPlusMetaLeafNode` to hash a transaction
     *  together with its execution metadata. Distinct from `TransactionId`
     *  so a raw transaction and its annotated form can never collide.
     */
    TxNode = detail::makeHashPrefix('S', 'N', 'D'),

    /** Account-state leaf node in the SHAMap (`MLN\0`). Used by
     *  `SHAMapAccountStateLeafNode` when computing or verifying the hash of
     *  a single ledger-state entry.
     */
    LeafNode = detail::makeHashPrefix('M', 'L', 'N'),

    /** SHAMap inner (branch) node (`MIN\0`). Used by `SHAMapInnerNode`
     *  when hashing the 16 child-hash slots of a branch node. Distinct from
     *  `LeafNode` so inner-node hashes never collide with leaf-node hashes.
     */
    InnerNode = detail::makeHashPrefix('M', 'I', 'N'),

    /** Ledger header signing payload (`LWR\0`). Prepended to the serialized
     *  ledger header before computing the ledger hash that validators sign and
     *  that serves as the canonical ledger identifier.
     */
    LedgerMaster = detail::makeHashPrefix('L', 'W', 'R'),

    /** Single-signature transaction signing payload (`STX\0`). Prepended to
     *  the serialized transaction body (with signing fields, without the
     *  signature itself) before a regular key or master key signs. A
     *  `TxSign` blob cannot be replayed as a `TxMultiSign` contribution
     *  because the two prefixes produce different digests.
     */
    TxSign = detail::makeHashPrefix('S', 'T', 'X'),

    /** Multi-signature transaction signing payload (`SMT\0`). Prepended to
     *  the serialized transaction body plus the signer's `AccountID` suffix
     *  before each individual signer's key signs. Distinct from `TxSign` to
     *  prevent a single-sig blob from being replayed as a multi-sig share.
     */
    TxMultiSign = detail::makeHashPrefix('S', 'M', 'T'),

    /** Validator validation message signing payload (`VAL\0`). Used by
     *  `STValidation::getSigningHash` to produce the digest that a validator
     *  signs when asserting agreement on a ledger.
     */
    Validation = detail::makeHashPrefix('V', 'A', 'L'),

    /** Consensus proposal signing payload (`PRP\0`). Used by
     *  `ConsensusProposal` and `RCLCxPeerPos` when signing or verifying a
     *  peer's position on a candidate ledger during the consensus round.
     */
    Proposal = detail::makeHashPrefix('P', 'R', 'P'),

    /** Validator manifest signing payload (`MAN\0`). Used by the manifest
     *  system to sign and verify the binding between a validator's master key
     *  and its rotating ephemeral signing key.
     */
    Manifest = detail::makeHashPrefix('M', 'A', 'N'),

    /** Off-ledger payment channel claim payload (`CLM\0`). Prepended to the
     *  channel ID and authorized amount before the channel owner signs an
     *  off-ledger claim that a counterparty can later submit on-chain.
     */
    PaymentChannelClaim = detail::makeHashPrefix('C', 'L', 'M'),

    /** Batch transaction signing payload (`BCH\0`). Prepended to the outer
     *  batch flags, inner transaction count, and list of inner transaction
     *  IDs before signing a batch. See `serializeBatch()` in `Batch.h`.
     */
    Batch = detail::makeHashPrefix('B', 'C', 'H'),
};

/** Feed a `HashPrefix` into a N3980-compatible streaming hasher.
 *
 *  Casts the prefix to its underlying `uint32_t` representation and forwards
 *  it to `beast::hash_append`, allowing a `HashPrefix` to be composed with
 *  other arguments in a single variadic `sha512Half` call:
 *  @code
 *  sha512Half(HashPrefix::transactionID, data)
 *  @endcode
 *  No temporary allocation or explicit serialization step is required; the
 *  4-byte prefix is fed directly into the running digest state.
 *
 *  @tparam Hasher A type satisfying the N3980 `hash_append` protocol
 *      (e.g. `sha512_half_hasher`).
 *  @param h The hasher instance to update.
 *  @param hp The prefix value to append.
 */
template <class Hasher>
void
hash_append(Hasher& h, HashPrefix const& hp) noexcept
{
    using beast::hash_append;
    hash_append(h, static_cast<std::uint32_t>(hp));
}

}  // namespace xrpl
