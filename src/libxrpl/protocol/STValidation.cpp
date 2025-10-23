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
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <utility>

namespace ripple {

STBase*
STValidation::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STValidation::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SOTemplate const&
STValidation::validationFormat()
{
    // We can't have this be a magic static at namespace scope because
    // it relies on the SField's below being initialized, and we can't
    // guarantee the initialization order.
    // clang-format off
    static SOTemplate const format{
        {sfFlags,               soeREQUIRED},
        {sfLedgerHash,          soeREQUIRED},
        {sfLedgerSequence,      soeREQUIRED},
        {sfCloseTime,           soeOPTIONAL},
        {sfLoadFee,             soeOPTIONAL},
        {sfAmendments,          soeOPTIONAL},
        {sfBaseFee,             soeOPTIONAL},
        {sfReserveBase,         soeOPTIONAL},
        {sfReserveIncrement,    soeOPTIONAL},
        {sfSigningTime,         soeREQUIRED},
        {sfSigningPubKey,       soeREQUIRED},
        {sfSignature,           soeREQUIRED},
        {sfConsensusHash,       soeOPTIONAL},
        // featureHardenedValidations
        {sfCookie,              soeDEFAULT},
        {sfValidatedHash,       soeOPTIONAL},
        {sfServerVersion,       soeOPTIONAL},
        // featureXRPFees
        {sfBaseFeeDrops,          soeOPTIONAL},
        {sfReserveBaseDrops,      soeOPTIONAL},
        {sfReserveIncrementDrops, soeOPTIONAL},
    };
    // clang-format on

    return format;
};

uint256
STValidation::getSigningHash() const
{
    return STObject::getSigningHash(HashPrefix::validation);
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

NetClock::time_point
STValidation::getSignTime() const
{
    return NetClock::time_point{NetClock::duration{getFieldU32(sfSigningTime)}};
}

NetClock::time_point
STValidation::getSeenTime() const noexcept
{
    return seenTime_;
}

bool
STValidation::isValid() const noexcept
{
    if (!valid_)
    {
        XRPL_ASSERT(
            publicKeyType(getSignerPublic()) == KeyType::secp256k1,
            "ripple::STValidation::isValid : valid key type");

        valid_ = verifyDigest(
            getSignerPublic(),
            getSigningHash(),
            makeSlice(getFieldVL(sfSignature)),
            getFlags() & vfFullyCanonicalSig);
    }

    return valid_.value();
}

bool
STValidation::isFull() const noexcept
{
    return (getFlags() & vfFullValidation) != 0;
}

Blob
STValidation::getSignature() const
{
    return getFieldVL(sfSignature);
}

Blob
STValidation::getSerialized() const
{
    Serializer s;
    add(s);
    return s.peekData();
}

}  // namespace ripple
