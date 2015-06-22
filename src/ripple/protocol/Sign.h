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

#ifndef RIPPLE_PROTOCOL_SIGN_H_INCLUDED
#define RIPPLE_PROTOCOL_SIGN_H_INCLUDED

#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/STObject.h>
#include <utility>

namespace ripple {

/** Sign a STObject using any secret key.
    The signature is placed in sfSignature. If
    a signature already exists, it is overwritten.
*/
void
sign (STObject& st,
    HashPrefix const& prefix,
        KeyType type, SecretKey const& sk);

/** Verify the signature on a STObject.
    The signature must be contained in sfSignature.
*/
bool
verify (STObject const& st,
    HashPrefix const& prefix,
        PublicKey const& pk,
            bool mustBeFullyCanonical);

/** Return a Serializer suitable for computing a multisigning TxnSignature. */
Serializer
buildMultiSigningData (STObject const& obj, AccountID const& signingID);

/** Break the multi-signing hash computation into 2 parts for optimization.

    We can optimize verifying multiple multisignatures by splitting the
    data building into two parts;
     o A large part that is shared by all of the computations.
     o A small part that is unique to each signer in the multisignature.

    The following methods support that optimization:
     1. startMultiSigningData provides the large part which can be shared.
     2. finishMuiltiSigningData caps the passed in serializer with each
        signer's unique data.
*/
Serializer
startMultiSigningData (STObject const& obj);

inline void
finishMultiSigningData (AccountID const& signingID, Serializer& s)
{
    s.add160 (signingID);
}

} // ripple

#endif
