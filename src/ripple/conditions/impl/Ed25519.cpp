//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/conditions/impl/Ed25519.h>

#include <ed25519-donna/ed25519.h>

namespace ripple {
namespace cryptoconditions {

void
Ed25519::encodeFingerprint(der::Encoder& encoder) const
{
    encoder << std::tie(publicKey_);
}

bool
Ed25519::validate(Slice data) const
{
    return ed25519_sign_open(
               data.data(),
               data.size(),
               publicKey_.data(),
               signature_.data()) == 0;
}

std::uint64_t
Ed25519::derEncodedLength(
    boost::optional<der::GroupType> const& parentGroupType,
    der::TagMode encoderTagMode, der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleEncodedLengthHelper(
        *this, parentGroupType, encoderTagMode, traitsCache);
}

void
Ed25519::encode(cryptoconditions::der::Encoder& encoder) const
{
    cryptoconditions::der::withTupleEncodeHelper(*this, encoder);
}

void
Ed25519::decode(cryptoconditions::der::Decoder& decoder)
{
    cryptoconditions::der::withTupleDecodeHelper(*this, decoder);
}

bool
Ed25519::checkEqualForTesting(Fulfillment const& rhs) const
{
    if (auto c = dynamic_cast<Ed25519 const*>(&rhs))
        return c->publicKey_ == publicKey_ && c->signature_ == signature_;
    return false;
}

int
Ed25519::compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleCompareHelper(
        *this, rhs, traitsCache);
}


bool
Ed25519::validationDependsOnMessage() const
{
    return true;
}

}
}
