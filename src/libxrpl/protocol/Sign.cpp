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

#include <ripple/protocol/Sign.h>

namespace ripple {

void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField)
{
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    set(st, sigField, sign(type, sk, ss.slice()));
}

bool
verify(
    STObject const& st,
    HashPrefix const& prefix,
    PublicKey const& pk,
    SF_VL const& sigField)
{
    auto const sig = get(st, sigField);
    if (!sig)
        return false;
    Serializer ss;
    ss.add32(prefix);
    st.addWithoutSigningFields(ss);
    return verify(
        pk, Slice(ss.data(), ss.size()), Slice(sig->data(), sig->size()));
}

// Questions regarding buildMultiSigningData:
//
// Why do we include the Signer.Account in the blob to be signed?
//
// Unless you include the Account which is signing in the signing blob,
// you could swap out any Signer.Account for any other, which may also
// be on the SignerList and have a RegularKey matching the
// Signer.SigningPubKey.
//
// That RegularKey may be set to allow some 3rd party to sign transactions
// on the account's behalf, and that RegularKey could be common amongst all
// users of the 3rd party. That's just one example of sharing the same
// RegularKey amongst various accounts and just one vulnerability.
//
//   "When you have something that's easy to do that makes entire classes of
//    attacks clearly and obviously impossible, you need a damn good reason
//    not to do it."  --  David Schwartz
//
// Why would we include the signingFor account in the blob to be signed?
//
// In the current signing scheme, the account that a signer is `signing
// for/on behalf of` is the tx_json.Account.
//
// Later we might support more levels of signing.  Suppose Bob is a signer
// for Alice, and Carol is a signer for Bob, so Carol can sign for Bob who
// signs for Alice.  But suppose Alice has two signers: Bob and Dave.  If
// Carol is a signer for both Bob and Dave, then the signature needs to
// distinguish between Carol signing for Bob and Carol signing for Dave.
//
// So, if we support multiple levels of signing, then we'll need to
// incorporate the "signing for" accounts into the signing data as well.
Serializer
buildMultiSigningData(STObject const& obj, AccountID const& signingID)
{
    Serializer s{startMultiSigningData(obj)};
    finishMultiSigningData(signingID, s);
    return s;
}

Serializer
startMultiSigningData(STObject const& obj)
{
    Serializer s;
    s.add32(HashPrefix::txMultiSign);
    obj.addWithoutSigningFields(s);
    return s;
}

}  // namespace ripple
