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

#include <BeastConfig.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>

namespace ripple {

STValidation::STValidation (SerialIter& sit, bool checkSignature)
    : STObject (getFormat (), sit, sfValidation)
{
    auto const spk = getFieldVL(sfSigningPubKey);

    if (publicKeyType(makeSlice(spk)) != KeyType::secp256k1)
    {
        JLOG (debugLog().error())
            << "Invalid public key in validation" << getJson (0);
        Throw<std::runtime_error> ("Invalid public key in validation");
    }

    mNodeID = calcNodeID(PublicKey(makeSlice(spk)));
    assert (mNodeID.isNonZero ());

    if  (checkSignature && !isValid ())
    {
        JLOG (debugLog().error())
            << "Invalid signature in validation" << getJson (0);
        Throw<std::runtime_error> ("Invalid signature in validation");
    }
}

STValidation::STValidation (
        uint256 const& ledgerHash,
        NetClock::time_point signTime,
        PublicKey const& publicKey,
        bool isFull)
    : STObject (getFormat (), sfValidation)
    , mSeen (signTime)
{
    // This is our own public key and it should always be valid.
    if (!publicKeyType(publicKey))
        LogicError ("Invalid validation public key");

    // Does not sign
    setFieldH256 (sfLedgerHash, ledgerHash);
    setFieldU32 (sfSigningTime, signTime.time_since_epoch().count());
    setFieldVL (sfSigningPubKey, publicKey.slice());

    mNodeID = calcNodeID(publicKey);
    assert (mNodeID.isNonZero ());

    if (isFull)
        setFlag (kFullFlag);
}

uint256 STValidation::sign (SecretKey const& secretKey)
{
    setFlag (vfFullyCanonicalSig);

    auto const signingHash = getSigningHash();
    setFieldVL (sfSignature,
        signDigest (getSignerPublic(), secretKey, signingHash));
    return signingHash;
}

uint256 STValidation::getSigningHash () const
{
    return STObject::getSigningHash (HashPrefix::validation);
}

uint256 STValidation::getLedgerHash () const
{
    return getFieldH256 (sfLedgerHash);
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
    return isValid (getSigningHash ());
}

bool STValidation::isValid (uint256 const& signingHash) const
{
    try
    {
        if (publicKeyType(getSignerPublic()) != KeyType::secp256k1)
            return false;

        return verifyDigest (getSignerPublic(),
            signingHash,
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
        }
    };

    static const FormatHolder holder;

    return holder.format;
}

} // ripple
