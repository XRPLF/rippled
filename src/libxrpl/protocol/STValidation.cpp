/** @file
 *  Implements the non-template methods of STValidation — a signed ledger
 *  agreement broadcast by a validator during each consensus round.
 *
 *  The template constructors and hot-path inlines are defined in
 *  STValidation.h. This file owns the field schema, cryptographic
 *  verification, and serialization helpers.
 */

#include <xrpl/protocol/STValidation.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <utility>

namespace xrpl {

/** Place-construct a copy of this object into a caller-supplied buffer.
 *
 *  Implements the `STBase` polymorphic copy protocol used by `STVar`'s
 *  small-object storage. The caller guarantees `buf` is at least `n` bytes.
 *
 *  @param n   Size of the buffer in bytes.
 *  @param buf Destination buffer; receives a placement-new'd copy.
 *  @return Pointer to the constructed object inside `buf`.
 */
STBase*
STValidation::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Place-construct a moved instance of this object into a caller-supplied buffer.
 *
 *  Implements the `STBase` polymorphic move protocol used by `STVar`'s
 *  small-object storage. The caller guarantees `buf` is at least `n` bytes.
 *
 *  @param n   Size of the buffer in bytes.
 *  @param buf Destination buffer; receives a placement-new'd move-constructed instance.
 *  @return Pointer to the constructed object inside `buf`.
 */
STBase*
STValidation::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

/** Return the field schema for STValidation objects.
 *
 *  Defines every field that may appear in a serialized validation, along
 *  with its presence rule (`SoeRequired`, `SoeOptional`, or `SoeDefault`).
 *  Required fields form the non-negotiable nucleus: `sfFlags`,
 *  `sfLedgerHash`, `sfLedgerSequence`, `sfSigningTime`, `sfSigningPubKey`,
 *  and `sfSignature`. Optional fields carry advisory network-state data
 *  (fee schedule, pending amendments) that validators may publish.
 *
 *  The three `sfBaseFeeDrops` / `sfReserveBaseDrops` /
 *  `sfReserveIncrementDrops` entries are added by the XRPFees amendment
 *  and coexist with the legacy `sfBaseFee` / `sfReserveBase` /
 *  `sfReserveIncrement` fields to allow gradual network adoption.
 *
 *  `sfCookie` is `SoeDefault`: always present in the serialized form but
 *  zero-valued unless explicitly set, which prevents fingerprinting
 *  validators that do not populate it.
 *
 *  @note The template is a function-local static rather than a
 *      namespace-scope global. This defers initialization to first call,
 *      by which time all `SField` singletons are guaranteed to be alive —
 *      C++ provides no cross-translation-unit initialization order for
 *      namespace-scope statics.
 *
 *  @return A reference to the single immutable `SOTemplate` instance.
 */
SOTemplate const&
STValidation::validationFormat()
{
    // clang-format off
    static SOTemplate const kFORMAT{
        {sfFlags,               SoeRequired},
        {sfLedgerHash,          SoeRequired},
        {sfLedgerSequence,      SoeRequired},
        {sfCloseTime,           SoeOptional},
        {sfLoadFee,             SoeOptional},
        {sfAmendments,          SoeOptional},
        {sfBaseFee,             SoeOptional},
        {sfReserveBase,         SoeOptional},
        {sfReserveIncrement,    SoeOptional},
        {sfSigningTime,         SoeRequired},
        {sfSigningPubKey,       SoeRequired},
        {sfSignature,           SoeRequired},
        {sfConsensusHash,       SoeOptional},
        {sfCookie,              SoeDefault},
        {sfValidatedHash,       SoeOptional},
        {sfServerVersion,       SoeOptional},
        // featureXRPFees
        {sfBaseFeeDrops,          SoeOptional},
        {sfReserveBaseDrops,      SoeOptional},
        {sfReserveIncrementDrops, SoeOptional},
    };
    // clang-format on

    return kFORMAT;
};

/** Compute the domain-separated hash that was (or will be) signed.
 *
 *  Prepends `HashPrefix::Validation` (the 4-byte big-endian encoding of
 *  `'V','A','L',0x00`) to the canonical serialization of all signed fields
 *  before hashing with SHA-512-Half. The prefix ensures a validation hash
 *  can never collide with a transaction or any other signed payload type.
 *
 *  @return The 256-bit digest over the validation's signed content.
 *  @see HashPrefix::Validation
 */
uint256
STValidation::getSigningHash() const
{
    return STObject::getSigningHash(HashPrefix::Validation);
}

uint256
STValidation::getLedgerHash() const
{
    return getFieldH256(sfLedgerHash);
}

uint256
STValidation::getConsensusHash() const
{
    return getFieldH256(sfConsensusHash);
}

/** Reconstruct the NetClock time at which the validator signed this message.
 *
 *  Reads `sfSigningTime` from the serialized object. This is the validator's
 *  own claim about when it signed; it is part of the signed payload and
 *  therefore cannot be forged without invalidating the signature.
 *
 *  @note This is distinct from `getSeenTime()`, which records when the
 *      *local* node received the message and is never serialized. Staleness
 *      checks use both: sign time to detect a validator drifting far from
 *      network time, seen time to detect messages that arrived too late
 *      regardless of the sender's clock.
 *
 *  @return The signing instant as a `NetClock::time_point`.
 */
NetClock::time_point
STValidation::getSignTime() const
{
    return NetClock::time_point{NetClock::duration{getFieldU32(sfSigningTime)}};
}

/** Return the local receive time recorded by this node for this validation.
 *
 *  `seenTime_` is set via `setSeen()` when the message arrives over the
 *  network; it is never serialized or signed. For self-issued validations
 *  the constructor initializes it to the signing time.
 *
 *  @return The instant this node observed the validation.
 */
NetClock::time_point
STValidation::getSeenTime() const noexcept
{
    return seenTime_;
}

/** Verify the cryptographic signature, caching the result.
 *
 *  On the first call, performs ECDSA signature verification via
 *  `verifyDigest()` using the pre-computed `getSigningHash()` digest.
 *  The `kVF_FULLY_CANONICAL_SIG` flag is passed to `verifyDigest()` to
 *  enforce low-S canonicality, preventing ECDSA signature malleability.
 *  The result is stored in `valid_` and returned on all subsequent calls
 *  without re-verifying.
 *
 *  For self-issued validations the signing constructor sets `valid_ = true`
 *  immediately after calling `signDigest()`, so this method never performs
 *  cryptographic work for locally created validations.
 *
 *  @note An assertion guards that the signing public key is `secp256k1`.
 *      If it fires, a non-ECDSA key was stored in `signingPubKey_`, which
 *      indicates a logic error in the deserialization path — the
 *      constructor is supposed to throw before that can happen.
 *
 *  @return `true` if the signature is valid; `false` otherwise.
 */
bool
STValidation::isValid() const noexcept
{
    if (!valid_)
    {
        XRPL_ASSERT(
            publicKeyType(getSignerPublic()) == KeyType::Secp256k1,
            "xrpl::STValidation::isValid : valid key type");

        valid_ = verifyDigest(
            getSignerPublic(),
            getSigningHash(),
            makeSlice(getFieldVL(sfSignature)),
            (getFlags() & kVF_FULLY_CANONICAL_SIG) != 0u);
    }

    return valid_.value();
}

bool
STValidation::isFull() const noexcept
{
    return (getFlags() & kVF_FULL_VALIDATION) != 0;
}

Blob
STValidation::getSignature() const
{
    return getFieldVL(sfSignature);
}

/** Serialize this validation to its canonical binary wire format.
 *
 *  The returned bytes include all fields (including the signature) and are
 *  suitable for network transmission or for computing a deduplication hash.
 *  Callers that need to suppress duplicate relays typically hash this output
 *  with `sha512Half`.
 *
 *  @return A `Blob` containing the complete serialized validation.
 */
Blob
STValidation::getSerialized() const
{
    Serializer s;
    add(s);
    return s.peekData();
}

}  // namespace xrpl
