/** @file
 *  Defines `LedgerHeader`, the compact canonical summary of a single XRP
 *  Ledger, together with the serialization, deserialization, and hash
 *  calculation functions that operate on it.
 *
 *  Every ledger — open, closed, or validated — is identified and
 *  authenticated through this structure. The serialized form is
 *  protocol-immutable: 118 bytes without the trailing hash, 150 with it.
 */

#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/** Canonical metadata block that identifies a single XRP Ledger.
 *
 *  Fields are split into two groups: those valid for all ledgers (including
 *  open ones that are still accumulating transactions) and those that are
 *  only meaningful once the ledger is closed (transaction set finalized).
 *
 *  `LedgerHeader` is embedded inside the `ReadView`/`ApplyView` hierarchy
 *  and is accessible via `view.info()`. The struct is intentionally small so
 *  it can be cheaply copied, compared, and transmitted without loading the
 *  full account-state SHAMap.
 *
 *  @note `validated` is `mutable` because it transitions one-way from
 *      `false` to `true` and must remain settable even on `const`-qualified
 *      ledger objects. This is a known design wart.
 *
 *  @see calculateLedgerHash, addRaw, deserializeHeader
 */
struct LedgerHeader
{
    explicit LedgerHeader() = default;

    //
    // For all ledgers
    //

    /** Monotonically increasing sequence number that identifies this ledger's
     *  position in the chain. */
    LedgerIndex seq = 0;

    /** Close time of the parent (previous) ledger, in `NetClock` seconds
     *  (epoch: 2000-01-01 00:00:00 UTC). */
    NetClock::time_point parentCloseTime;

    //
    // For closed ledgers
    //

    // Closed means "tx set already determined"

    /** This ledger's own identity hash, computed by `calculateLedgerHash`.
     *  Meaningful only after the ledger is closed. */
    uint256 hash = beast::kZERO;

    /** SHAMap root hash of the transaction set for this ledger. */
    uint256 txHash = beast::kZERO;

    /** SHAMap root hash of the account-state tree after applying this
     *  ledger's transaction set. */
    uint256 accountHash = beast::kZERO;

    /** Hash of the immediately preceding ledger; links this ledger into the
     *  chain and is included in the signed hash. */
    uint256 parentHash = beast::kZERO;

    /** Total XRP in existence at this ledger, in drops (1 XRP = 10^6 drops). */
    XRPAmount drops = beast::kZERO;

    /** Whether this ledger has been confirmed by a quorum of validators.
     *  Transitions one-way from `false` to `true`; never reverts.
     *  Declared `mutable` so it can be set on `const`-qualified objects. */
    bool mutable validated = false;

    /** Whether this node has accepted the ledger's transaction set,
     *  independent of network-wide validation. */
    bool accepted = false;

    /** Bitmask of close-time flags produced by the consensus round.
     *  The only defined bit is `kS_LCF_NO_CONSENSUS_TIME` (0x01).
     *  Serialized as a single `uint8_t`; only the low 8 bits are meaningful. */
    int closeFlags = 0;

    /** Granularity to which the close time was rounded, in seconds (2–120).
     *  Determined by the consensus algorithm and stored per-ledger. */
    NetClock::duration closeTimeResolution = {};

    /** For closed ledgers: the time at which the ledger closed, in
     *  `NetClock` seconds. For open ledgers: the projected close time if
     *  no transactions arrive. */
    NetClock::time_point closeTime;
};

/** Close-flag bit set when consensus could not agree on a close time.
 *
 *  Written into `LedgerHeader::closeFlags` during `Ledger::setAccepted()`
 *  when the `correctCloseTime` argument is `false`. Queried via
 *  `getCloseAgree()`.
 */
static std::uint32_t const kS_LCF_NO_CONSENSUS_TIME = 0x01;

/** Return `true` if the consensus round agreed on a close time for this
 *  ledger.
 *
 *  Returns `false` when `kS_LCF_NO_CONSENSUS_TIME` is set in
 *  `info.closeFlags`, which indicates the validator set was unable to
 *  reach agreement (e.g., significant clock skew or a very small validator
 *  set). Callers that require a reliable close time — such as the ledger
 *  replay subsystem — must guard on this value.
 *
 *  @param info The ledger header to inspect.
 *  @return `true` if `kS_LCF_NO_CONSENSUS_TIME` is not set; `false`
 *      otherwise.
 */
inline bool
getCloseAgree(LedgerHeader const& info)
{
    return (info.closeFlags & kS_LCF_NO_CONSENSUS_TIME) == 0;
}

/** Append the ledger header to a serializer in canonical network byte order.
 *
 *  Field order (protocol-immutable): `seq` (32-bit), `drops` (64-bit),
 *  `parentHash`, `txHash`, `accountHash` (each 256-bit), `parentCloseTime`,
 *  `closeTime` (each 32-bit epoch seconds), `closeTimeResolution` (8-bit),
 *  `closeFlags` (8-bit). Total: 118 bytes. When `includeHash` is `true`,
 *  `hash` is appended as an additional 32 bytes.
 *
 *  The hash is omitted by default to avoid circularity: it is derived from
 *  all other fields and must not be part of the input to
 *  `calculateLedgerHash`. Pass `includeHash = true` when persisting to the
 *  node store or transmitting over the wire so receivers can skip
 *  recomputing it.
 *
 *  @note The field order here must exactly mirror `calculateLedgerHash`.
 *      They are not mechanically linked; a divergence silently breaks
 *      consensus-level hash agreement across the network.
 *  @note `validated` and `accepted` are runtime-only flags and are not
 *      written to the serializer.
 *
 *  @param info        The ledger header to serialize.
 *  @param s           Accumulator that receives the serialized bytes.
 *  @param includeHash If `true`, append `info.hash` after all other fields.
 */
void
addRaw(LedgerHeader const&, Serializer&, bool includeHash = false);

/** Deserialize a ledger header from a raw byte buffer.
 *
 *  Reads fields in the same order that `addRaw` writes them. Time fields
 *  are raw 32-bit epoch counts (seconds since 2000-01-01 00:00:00 UTC)
 *  and are restored to typed `NetClock::time_point` values. The runtime-only
 *  fields `validated` and `accepted` are left at their default values.
 *
 *  No semantic validation is performed beyond what `SerialIter` enforces.
 *  The caller is responsible for verifying that the deserialized `hash`
 *  matches `calculateLedgerHash` before trusting the data.
 *
 *  @param data    View over the raw bytes to deserialize.
 *  @param hasHash If `true`, read a trailing 256-bit value into
 *      `LedgerHeader::hash`. Must match how the header was serialized.
 *  @return A populated `LedgerHeader`.
 *  @throws std::runtime_error (via `SerialIter`) if @p data is shorter
 *      than the expected field sequence.
 */
LedgerHeader
deserializeHeader(Slice data, bool hasHash = false);

/** Deserialize a ledger header that is preceded by a 4-byte prefix.
 *
 *  Skips the leading `HashPrefix` tag that is prepended when a ledger
 *  header is stored in the node database or transmitted in a peer-protocol
 *  message, then delegates to `deserializeHeader`.
 *
 *  @param data    View over the raw bytes, including the 4-byte prefix.
 *  @param hasHash Forwarded to `deserializeHeader`; see its documentation.
 *  @return A populated `LedgerHeader` as returned by `deserializeHeader`.
 *  @throws std::runtime_error (via `SerialIter`) if the buffer after
 *      skipping the prefix is too short.
 */
LedgerHeader
deserializePrefixedHeader(Slice data, bool hasHash = false);

/** Compute the canonical 256-bit identity hash for a ledger header.
 *
 *  Feeds all header fields (except `hash` itself, `validated`, and
 *  `accepted`) into `sha512Half` — the first 256 bits of a SHA-512 digest
 *  — prepended with `HashPrefix::LedgerMaster` (`LWR\0`). The four-byte
 *  prefix provides hash-domain separation, preventing collisions with
 *  hashes computed over other XRPL object types.
 *
 *  Each field is explicitly cast to its protocol-defined wire width before
 *  hashing, preventing silent integer widening from diverging from the
 *  network.
 *
 *  @note The field order and widths here must exactly mirror `addRaw`.
 *      They are not mechanically linked; a divergence causes this node to
 *      compute hashes that disagree with the rest of the network.
 *
 *  @param info The ledger header to hash.
 *  @return The 256-bit canonical ledger hash.
 */
uint256
calculateLedgerHash(LedgerHeader const& info);

}  // namespace xrpl
