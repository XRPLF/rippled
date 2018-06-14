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

    if (fees.baseFee)
        setFieldU64(sfBaseFee, *fees.baseFee);

    if (fees.reserveBase)
        setFieldU32(sfReserveBase, *fees.reserveBase);

    if (fees.reserveIncrement)
        setFieldU32(sfReserveIncrement, *fees.reserveIncrement);

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
        SOTemplate format;

        FormatHolder ()
        {
            format.push_back (SOElement (sfFlags,           SOE_REQUIRED));
            format.push_back (SOElement (sfLedgerHash,      SOE_REQUIRED));
            format.push_back (SOElement (sfLedgerSequence,  SOE_OPTIONAL));
            format.push_back (SOElement (sfCloseTime,       SOE_OPTIONAL));
            format.push_back (SOElement (sfLoadFee,         SOE_OPTIONAL));
            format.push_back (SOElement (sfAmendments,      SOE_OPTIONAL));
            format.push_back (SOElement (sfBaseFee,         SOE_OPTIONAL));
            format.push_back (SOElement (sfReserveBase,     SOE_OPTIONAL));
            format.push_back (SOElement (sfReserveIncrement, SOE_OPTIONAL));
            format.push_back (SOElement (sfSigningTime,     SOE_REQUIRED));
            format.push_back (SOElement (sfSigningPubKey,   SOE_REQUIRED));
            format.push_back (SOElement (sfSignature,       SOE_OPTIONAL));
            format.push_back (SOElement (sfConsensusHash,   SOE_OPTIONAL));
            format.push_back (SOElement (sfCookie,          SOE_OPTIONAL));

        }
    };

    static const FormatHolder holder;

    return holder.format;
}

} // ripple
