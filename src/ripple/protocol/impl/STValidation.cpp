//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>

namespace ripple {

STValidation::STValidation(
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    uint256 const& consensusHash,
    NetClock::time_point signTime,
    PublicKey const& publicKey,
    SecretKey const& secretKey,
    NodeID const& nodeID,
    bool isFull,
    FeeSettings const& fees,
    std::vector<uint256> const& amendments)
    : STObject(getFormat(), sfValidation), mNodeID(nodeID), mSeen(signTime)
{
    // This is our own public key and it should always be valid.
    if (!publicKeyType(publicKey))
        LogicError("Invalid validation public key");
    assert(mNodeID.isNonZero());
    setFieldH256(sfLedgerHash, ledgerHash);
    setFieldH256(sfConsensusHash, consensusHash);
    setFieldU32(sfSigningTime, signTime.time_since_epoch().count());

    setFieldVL(sfSigningPubKey, publicKey.slice());
    if (isFull)
        setFlag(kFullFlag);

    setFieldU32(sfLedgerSequence, ledgerSeq);

    if (fees.loadFee)
        setFieldU32(sfLoadFee, *fees.loadFee);

    // IF any of the values are out of the valid range, don't send a value.
    // They should not be an issue, though, because the voting
    // process (FeeVoteImpl) ignores any out of range values.
    if (fees.baseFee)
    {
        if (auto const v = fees.baseFee->dropsAs<std::uint64_t>())
            setFieldU64(sfBaseFee, *v);
    }

    if (fees.reserveBase)
    {
        if (auto const v = fees.reserveBase->dropsAs<std::uint32_t>())
            setFieldU32(sfReserveBase, *v);
    }

    if (fees.reserveIncrement)
    {
        if (auto const v = fees.reserveIncrement->dropsAs<std::uint32_t>())
            setFieldU32(sfReserveIncrement, *v);
    }

    if (!amendments.empty())
        setFieldV256(sfAmendments, STVector256(sfAmendments, amendments));

    setFlag(vfFullyCanonicalSig);

    auto const signingHash = getSigningHash();
    setFieldVL(
        sfSignature, signDigest(getSignerPublic(), secretKey, signingHash));

    setTrusted();
}

uint256 STValidation::getSigningHash () const
{
    return STObject::getSigningHash (HashPrefix::validation);
}

uint256 STValidation::getLedgerHash () const
{
    return getFieldH256 (sfLedgerHash);
}

uint256 STValidation::getConsensusHash () const
{
    return getFieldH256 (sfConsensusHash);
}

NetClock::time_point
STValidation::getSignTime () const
{
    return NetClock::time_point{NetClock::duration{getFieldU32(sfSigningTime)}};
}

NetClock::time_point STValidation::getSeenTime () const
{
    return mSeen;
}

bool STValidation::isValid () const
{
    try
    {
        if (publicKeyType(getSignerPublic()) != KeyType::secp256k1)
            return false;

        return verifyDigest (getSignerPublic(), getSigningHash(),
            makeSlice(getFieldVL (sfSignature)),
            getFlags () & vfFullyCanonicalSig);
    }
    catch (std::exception const&)
    {
        JLOG (debugLog().error())
            << "Exception validating validation";
        return false;
    }
}

PublicKey STValidation::getSignerPublic () const
{
    return PublicKey(makeSlice (getFieldVL (sfSigningPubKey)));
}

bool STValidation::isFull () const
{
    return (getFlags () & kFullFlag) != 0;
}

Blob STValidation::getSignature () const
{
    return getFieldVL (sfSignature);
}

Blob STValidation::getSerialized () const
{
    Serializer s;
    add (s);
    return s.peekData ();
}

SOTemplate const& STValidation::getFormat ()
{
    struct FormatHolder
    {
        SOTemplate format
        {
            { sfFlags,            soeREQUIRED },
            { sfLedgerHash,       soeREQUIRED },
            { sfLedgerSequence,   soeOPTIONAL },
            { sfCloseTime,        soeOPTIONAL },
            { sfLoadFee,          soeOPTIONAL },
            { sfAmendments,       soeOPTIONAL },
            { sfBaseFee,          soeOPTIONAL },
            { sfReserveBase,      soeOPTIONAL },
            { sfReserveIncrement, soeOPTIONAL },
            { sfSigningTime,      soeREQUIRED },
            { sfSigningPubKey,    soeREQUIRED },
            { sfSignature,        soeOPTIONAL },
            { sfConsensusHash,    soeOPTIONAL },
            { sfCookie,           soeOPTIONAL },
        };
    };

    static const FormatHolder holder;

    return holder.format;
}

} // ripple
