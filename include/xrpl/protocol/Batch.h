/** @file
 *  Defines the canonical wire-format serialization for batch signing payloads.
 *
 *  A batch payload is the exact byte sequence that every co-signer of a
 *  `ttBATCH` transaction signs and that validators verify. The format is
 *  protocol-stable: any reordering of the four serialized fields would
 *  invalidate all previously issued batch signatures.
 */

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

/** Serialize the signable payload for a batch transaction.
 *
 *  Appends four fields to `msg` in a fixed order:
 *  1. `HashPrefix::batch` — 4-byte domain separator that places batch hashes
 *     in their own hash-space, preventing cross-type signature collisions.
 *  2. `flags` — the outer batch transaction's execution-policy flags (e.g.
 *     `tfAllOrNothing`). Signing over the flags ensures a signer cannot have
 *     the execution policy changed after they have committed.
 *  3. `txids.size()` — the inner-transaction count as a `uint32_t`. Explicit
 *     serialization of the count prevents an adversary from extending or
 *     truncating the list without invalidating signatures.
 *  4. Each `uint256` in `txids` — the hash of each inner transaction, in
 *     order. Signers commit to the exact set of inner transactions by ID.
 *
 *  Both `checkBatchSingleSign()` and `checkBatchMultiSign()` in `STTx.cpp`
 *  call this function to build the verification payload, and test signing
 *  helpers do the same, so signing and verification share a single
 *  serialization path. For multi-sign, `serializeBatch()` is called once
 *  and `finishMultiSigningData()` appends each per-signer account ID suffix
 *  without re-serializing the inner transaction list.
 *
 *  @param msg   Serializer that receives the batch payload bytes. The caller
 *      is responsible for passing the resulting `msg.slice()` to the
 *      appropriate signature primitive.
 *  @param flags The `uint32_t` flags field of the outer batch transaction,
 *      as returned by `STTx::getFlags()`.
 *  @param txids Ordered list of inner-transaction IDs, as returned by
 *      `STTx::getBatchTransactionIDs()`.
 *
 *  @note `HashPrefix::batch` is a protocol constant. Changing it would
 *      invalidate all existing batch signatures and requires an amendment.
 */
inline void
serializeBatch(Serializer& msg, std::uint32_t const& flags, std::vector<uint256> const& txids)
{
    msg.add32(HashPrefix::Batch);
    msg.add32(flags);
    msg.add32(std::uint32_t(txids.size()));
    for (auto const& txid : txids)
        msg.addBitString(txid);
}

}  // namespace xrpl
