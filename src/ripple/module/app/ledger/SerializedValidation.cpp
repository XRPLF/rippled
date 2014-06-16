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

namespace ripple {

SerializedValidation::SerializedValidation (SerializerIterator& sit, bool checkSignature)
    : STObject (getFormat (), sit, sfValidation)
    , mTrusted (false)
{
    mNodeID = RippleAddress::createNodePublic (getFieldVL (sfSigningPubKey)).getNodeID ();
    assert (mNodeID.isNonZero ());

    if  (checkSignature && !isValid ())
    {
        Log (lsTRACE) << "Invalid validation " << getJson (0);
        throw std::runtime_error ("Invalid validation");
    }
}

SerializedValidation::SerializedValidation (
    uint256 const& ledgerHash, std::uint32_t signTime,
    const RippleAddress& raPub, bool isFull)
    : STObject (getFormat (), sfValidation)
    , mTrusted (false)
{
    // Does not sign
    setFieldH256 (sfLedgerHash, ledgerHash);
    setFieldU32 (sfSigningTime, signTime);

    setFieldVL (sfSigningPubKey, raPub.getNodePublic ());
    mNodeID = raPub.getNodeID ();
    assert (mNodeID.isNonZero ());

    if (!isFull)
        setFlag (kFullFlag);
}

void SerializedValidation::sign (const RippleAddress& raPriv)
{
    uint256 signingHash;
    sign (signingHash, raPriv);
}

void SerializedValidation::sign (uint256& signingHash, const RippleAddress& raPriv)
{
    setFlag (vfFullyCanonicalSig);

    signingHash = getSigningHash ();
    Blob signature;
    raPriv.signNodePrivate (signingHash, signature);
    setFieldVL (sfSignature, signature);
}

uint256 SerializedValidation::getSigningHash () const
{
    return STObject::getSigningHash (getConfig ().SIGN_VALIDATION);
}

uint256 SerializedValidation::getLedgerHash () const
{
    return getFieldH256 (sfLedgerHash);
}

std::uint32_t SerializedValidation::getSignTime () const
{
    return getFieldU32 (sfSigningTime);
}

std::uint32_t SerializedValidation::getFlags () const
{
    return getFieldU32 (sfFlags);
}

bool SerializedValidation::isValid () const
{
    return isValid (getSigningHash ());
}

bool SerializedValidation::isValid (uint256 const& signingHash) const
{
    try
    {
        const ECDSA fullyCanonical = getFlags () & vfFullyCanonicalSig ?
                                            ECDSA::strict : ECDSA::not_strict;
        RippleAddress   raPublicKey = RippleAddress::createNodePublic (getFieldVL (sfSigningPubKey));
        return raPublicKey.isValid () &&
            raPublicKey.verifyNodePublic (signingHash, getFieldVL (sfSignature), fullyCanonical);
    }
    catch (...)
    {
        Log (lsINFO) << "exception validating validation";
        return false;
    }
}

RippleAddress SerializedValidation::getSignerPublic () const
{
    RippleAddress a;
    a.setNodePublic (getFieldVL (sfSigningPubKey));
    return a;
}

bool SerializedValidation::isFull () const
{
    return (getFlags () & kFullFlag) != 0;
}

Blob SerializedValidation::getSignature () const
{
    return getFieldVL (sfSignature);
}

Blob SerializedValidation::getSigned () const
{
    Serializer s;
    add (s);
    return s.peekData ();
}

SOTemplate const& SerializedValidation::getFormat ()
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
        }
    };

    static FormatHolder holder;

    return holder.format;
}

} // ripple
