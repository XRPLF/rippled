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

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>

namespace ripple {

/** Sign an STObject

    @param st Object to sign
    @param prefix Prefix to insert before serialized object when hashing
    @param type Signing key type used to derive public key
    @param sk Signing secret key
    @param sigField Field in which to store the signature on the object.
    If not specified the value defaults to `sfSignature`.

    @note If a signature already exists, it is overwritten.
*/
void
sign(
    STObject& st,
    HashPrefix const& prefix,
    KeyType type,
    SecretKey const& sk,
    SF_VL const& sigField = sfSignature);

/** Returns `true` if STObject contains valid signature

    @param st Signed object
    @param prefix Prefix inserted before serialized object when hashing
    @param pk Public key for verifying signature
    @param sigField Object's field containing the signature.
    If not specified the value defaults to `sfSignature`.
*/
bool
verify(
    STObject const& st,
    HashPrefix const& prefix,
    PublicKey const& pk,
    SF_VL const& sigField = sfSignature);

}  // namespace ripple

#endif
