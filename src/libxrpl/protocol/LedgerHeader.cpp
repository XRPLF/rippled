/** @file
 *  Canonical serialization, deserialization, and hash calculation for
 *  `LedgerHeader` — the fixed-size metadata block that identifies every
 *  closed XRP Ledger.
 *
 *  Every path a ledger takes through the system (network propagation,
 *  node-store persistence, validation, and replay) passes through one or
 *  more of these four functions. The field order and integer widths used
 *  here are protocol-immutable: changing them without an amendment would
 *  produce hashes that diverge from the rest of the network.
 */

#include <xrpl/protocol/LedgerHeader.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>

#include <cstdint>

namespace xrpl {

/** Append a ledger header to a serializer in canonical network byte order.
 *
 *  Field order: seq (32-bit), drops (64-bit), parentHash, txHash,
 *  accountHash (each 256-bit), parentCloseTime, closeTime (each 32-bit
 *  epoch counts), closeTimeResolution (8-bit), closeFlags (8-bit). The
 *  `hash` field is appended last only when @p includeHash is `true`.
 *
 *  Omitting the hash by default avoids circularity: the hash is derived
 *  from all other fields, so it must not be part of the payload that is
 *  fed into `calculateLedgerHash`. Pass `includeHash = true` when
 *  persisting to the node store or transmitting over the wire so that
 *  receivers can skip recomputing it.
 *
 *  @note The field order here must exactly mirror `calculateLedgerHash`.
 *      They are not mechanically linked; a divergence silently breaks
 *      consensus-level hash agreement across the network.
 *
 *  @param info        The ledger header to serialize. `validated` and
 *      `accepted` are runtime-only flags and are not written.
 *  @param s           Accumulator that receives the serialized bytes.
 *  @param includeHash If `true`, append `info.hash` after all other fields.
 */
void
addRaw(LedgerHeader const& info, Serializer& s, bool includeHash)
{
    s.add32(info.seq);
    s.add64(info.drops.drops());
    s.addBitString(info.parentHash);
    s.addBitString(info.txHash);
    s.addBitString(info.accountHash);
    s.add32(info.parentCloseTime.time_since_epoch().count());
    s.add32(info.closeTime.time_since_epoch().count());
    s.add8(info.closeTimeResolution.count());
    s.add8(info.closeFlags);

    if (includeHash)
        s.addBitString(info.hash);
}

/** Deserialize a ledger header from a raw byte buffer.
 *
 *  Reads fields in the same order that `addRaw` writes them. Time fields
 *  are raw 32-bit epoch counts (seconds since the XRPL epoch, 1 Jan 2000)
 *  that are wrapped back into typed `NetClock::time_point` values.
 *
 *  No semantic validation is performed beyond what `SerialIter` enforces.
 *  The caller is responsible for verifying that the deserialized `hash`
 *  matches `calculateLedgerHash` before trusting the data.
 *
 *  @param data    View over the raw bytes to deserialize.
 *  @param hasHash If `true`, read a trailing 256-bit hash into
 *      `LedgerHeader::hash`. Pass `false` when the hash was omitted
 *      during serialization.
 *  @return        A populated `LedgerHeader`; `validated` and `accepted`
 *      are left at their default values.
 *  @throws        `std::runtime_error` (via `SerialIter`) if @p data is
 *      shorter than the expected field sequence.
 */
LedgerHeader
deserializeHeader(Slice data, bool hasHash)
{
    SerialIter sit(data.data(), data.size());

    LedgerHeader header;

    header.seq = sit.get32();
    header.drops = sit.get64();
    header.parentHash = sit.get256();
    header.txHash = sit.get256();
    header.accountHash = sit.get256();
    header.parentCloseTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    header.closeTime = NetClock::time_point{NetClock::duration{sit.get32()}};
    header.closeTimeResolution = NetClock::duration{sit.get8()};
    header.closeFlags = sit.get8();

    if (hasHash)
        header.hash = sit.get256();

    return header;
}

/** Deserialize a ledger header that was stored with a leading 4-byte prefix.
 *
 *  Advances past the `HashPrefix` tag that is prepended when a ledger header
 *  is stored in the node database or transmitted in a peer-protocol message,
 *  then delegates to `deserializeHeader`.
 *
 *  @param data    View over the raw bytes, including the 4-byte prefix.
 *  @param hasHash Forwarded to `deserializeHeader`; see its documentation.
 *  @return        A populated `LedgerHeader` as returned by `deserializeHeader`.
 *  @throws        `std::runtime_error` (via `SerialIter`) if the remaining
 *      buffer after skipping the prefix is too short.
 */
LedgerHeader
deserializePrefixedHeader(Slice data, bool hasHash)
{
    return deserializeHeader(data + 4, hasHash);
}

/** Compute the canonical 256-bit identity hash for a ledger header.
 *
 *  Feeds all header fields (except `hash` itself) into `sha512Half` —
 *  the first 256 bits of a SHA-512 digest — prepended with
 *  `HashPrefix::LedgerMaster` (`LWR\0`). The four-byte prefix ensures
 *  hash-domain separation: even identical binary content produces a
 *  different digest when hashed as a ledger vs. any other XRPL object type.
 *
 *  Explicit casts to `std::uint32_t`, `std::uint64_t`, and `std::uint8_t`
 *  in the call site pin each field to its protocol-defined wire width,
 *  preventing silent widening or narrowing from silently diverging from
 *  the network.
 *
 *  @note The field order and widths here must exactly mirror `addRaw`.
 *      They are not mechanically linked; a divergence causes this node to
 *      compute hashes that disagree with the rest of the network.
 *
 *  @param info The ledger header to hash. `hash`, `validated`, and
 *      `accepted` are not included in the digest.
 *  @return The 256-bit canonical ledger hash.
 */
uint256
calculateLedgerHash(LedgerHeader const& info)
{
    return sha512Half(
        HashPrefix::LedgerMaster,
        std::uint32_t(info.seq),
        std::uint64_t(info.drops.drops()),
        info.parentHash,
        info.txHash,
        info.accountHash,
        std::uint32_t(info.parentCloseTime.time_since_epoch().count()),
        std::uint32_t(info.closeTime.time_since_epoch().count()),
        std::uint8_t(info.closeTimeResolution.count()),
        std::uint8_t(info.closeFlags));
}

}  // namespace xrpl
