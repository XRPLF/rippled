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
#include <ripple/protocol/Sign.h>

namespace ripple {

void
sign (STObject& st, HashPrefix const& prefix,
    KeyType type, SecretKey const& sk)
{
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    set(st, sfSignature,
        sign(type, sk, ss.slice()));
}

bool
verify (STObject const& st,
    HashPrefix const& prefix,
        PublicKey const& pk,
            bool mustBeFullyCanonical)
{
    auto const sig = get(st, sfSignature);
    if (! sig)
        return false;
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    return pk.verify(
        Slice(ss.data(), ss.size()),
            Slice(sig->data(), sig->size()),
                true);
}

Serializer
buildMultiSigningData (STObject const& obj, AccountID const& signingID)
{
    Serializer s {startMultiSigningData (obj)};
    finishMultiSigningData (signingID, s);
    return s;
}

Serializer
startMultiSigningData (STObject const& obj)
{
    Serializer s;
    s.add32 (HashPrefix::txMultiSign);
    obj.addWithoutSigningFields (s);
    return s;
}

} // ripple
