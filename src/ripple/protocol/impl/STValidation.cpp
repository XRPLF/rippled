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

#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/STValidation.h>

namespace ripple {

SOTemplate const&
STValidation::validationFormat()
{
    // We can't have this be a magic static at namespace scope because
    // it relies on the SField's below being initialized, and we can't
    // guarantee the initialization order.
    static SOTemplate const format{
        {sfFlags, soeREQUIRED},
        {sfLedgerHash, soeREQUIRED},
        {sfLedgerSequence, soeREQUIRED},
        {sfCloseTime, soeOPTIONAL},
        {sfLoadFee, soeOPTIONAL},
        {sfAmendments, soeOPTIONAL},
        {sfBaseFee, soeOPTIONAL},
        {sfReserveBase, soeOPTIONAL},
        {sfReserveIncrement, soeOPTIONAL},
        {sfSigningTime, soeREQUIRED},
        {sfSigningPubKey, soeREQUIRED},
        {sfSignature, soeREQUIRED},
        {sfConsensusHash, soeOPTIONAL},
        {sfCookie, soeDEFAULT},
        {sfValidatedHash, soeOPTIONAL},
        {sfServerVersion, soeOPTIONAL},
    };

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
STValidation::getSeenTime() const
{
    return seenTime_;
}

bool
STValidation::isValid() const
{
    try
    {
        if (publicKeyType(getSignerPublic()) != KeyType::secp256k1)
            return false;

        return verifyDigest(
            getSignerPublic(),
            getSigningHash(),
            makeSlice(getFieldVL(sfSignature)),
            getFlags() & vfFullyCanonicalSig);
    }
    catch (std::exception const&)
    {
        JLOG(debugLog().error()) << "Exception validating validation";
        return false;
    }
}

PublicKey
STValidation::getSignerPublic() const
{
    return PublicKey(makeSlice(getFieldVL(sfSigningPubKey)));
}

bool
STValidation::isFull() const
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
