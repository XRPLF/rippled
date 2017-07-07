//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/impl/Der.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Ed25519.h>
#include <ripple/conditions/impl/PrefixSha256.h>
#include <ripple/conditions/impl/PreimageSha256.h>
#include <ripple/conditions/impl/RsaSha256.h>
#include <ripple/conditions/impl/ThresholdSha256.h>
#include <ripple/protocol/digest.h>

#include <boost/regex.hpp>
#include <boost/optional.hpp>

#include <type_traits>
#include <vector>

namespace ripple {
namespace cryptoconditions {

namespace der {

void
DerCoderTraits<std::unique_ptr<Fulfillment>>::
encode(
    Encoder& encoder,
    std::unique_ptr<Fulfillment> const& f)
{
    assert(f);
    f->encode(encoder);
}

void
DerCoderTraits<std::unique_ptr<Fulfillment>>::
decode(Decoder& decoder, std::unique_ptr<Fulfillment>& v)
{
    if (decoder.parentSlice().size() > Fulfillment::maxSerializedFulfillment)
    {
        decoder.ec_ = Error::largeSize;
        return;
    }

    auto const parentTag = decoder.parentTag();
    if (!parentTag)
    {
        decoder.ec_ = make_error_code(Error::logicError);
        return;
    }

    if (parentTag->classId != classId())
    {
        decoder.ec_ = make_error_code(Error::preambleMismatch);
        return;
    }

    if (parentTag->tagNum > static_cast<std::uint64_t>(Type::last))
    {
        decoder.ec_ = make_error_code(Error::preambleMismatch);
        return;
    }

    switch (parentTag->tagNum)
    {
        case static_cast<std::uint64_t>(Type::preimageSha256):
            v = std::make_unique<PreimageSha256>(der::constructor);
            break;
        case static_cast<std::uint64_t>(Type::prefixSha256):
            v = std::make_unique<PrefixSha256>(der::constructor);
            break;
        case static_cast<std::uint64_t>(Type::thresholdSha256):
            v = std::make_unique<ThresholdSha256>(der::constructor);
            break;
        case static_cast<std::uint64_t>(Type::rsaSha256):
            v = std::make_unique<RsaSha256>(der::constructor);
            break;
        case static_cast<std::uint64_t>(Type::ed25519Sha256):
            v = std::make_unique<Ed25519>(der::constructor);
            break;
        default:
            decoder.ec_ = Error::unsupportedType;
    }

    if (!v)
        return;
    v->decode(decoder);

    if (decoder.ec_)
        v.reset();
}

std::uint64_t
DerCoderTraits<std::unique_ptr<Fulfillment>>::
length(
    std::unique_ptr<Fulfillment> const& v,
    boost::optional<GroupType> const& parentGroupType,
    TagMode encoderTagMode,
    TraitsCache& traitsCache)
{
    if (auto cached = traitsCache.length(v.get()))
        return *cached;

    auto const l = v->derEncodedLength(parentGroupType, encoderTagMode, traitsCache);
    if (encoderTagMode == TagMode::automatic)
    {
        traitsCache.length(v.get(), l);
        return l;
    }

    static_assert(
        static_cast<std::uint32_t>(Type::last) < 30,
        "Tag will no longer fit in a byte");
    auto const result = 1 + l + contentLengthLength(l);
    traitsCache.length(v.get(), result);
    return result;
}

}  // der

Condition
Fulfillment::condition(std::error_code& ec) const
{
    return {type(), cost(), fingerprint(ec), subtypes()};
}

std::unique_ptr<Fulfillment>
Fulfillment::deserialize(
    Slice s,
    std::error_code& ec)
{
    using namespace der;

    std::unique_ptr<Fulfillment> v;

    der::Decoder decoder(s, der::TagMode::automatic);
    decoder >> v >> der::eos;
    ec = decoder.ec_;
    if (ec)
    {
        return {};
    }

    return v;
}

std::array<std::uint8_t, 32>
Fulfillment::fingerprint(std::error_code& ec) const
{
    der::Encoder encoder{der::TagMode::automatic};
    encodeFingerprint(encoder);
    encoder << der::eos;

    if (encoder.ec_)
    {
        ec = encoder.ec_;
        return {};
    }

    auto const& encoded = encoder.serializationBuffer(ec);
    if (ec)
        return {};

    sha256_hasher h;
    h(encoded.data(), encoded.size());
    return static_cast<sha256_hasher::result_type>(h);
}

std::bitset<5>
Fulfillment::selfAndSubtypes() const
{
    std::bitset<5> result;
    result.set(static_cast<std::size_t>(type()));
    result |= subtypes();
    return result;
}

bool
match (
    Fulfillment const& f,
    Condition const& c)
{
    // Fast check: the fulfillment's type must match the
    // conditions's type:
    if (f.type() != c.type)
        return false;

    // Derive the condition from the given fulfillment
    // and ensure that it matches the given condition.
    std::error_code ec;
    auto const result = c == f.condition(ec);
    if (ec)
        return false;
    return result;
}

bool
validate (
    Fulfillment const& f,
    Condition const& c,
    Slice m)
{
    return match (f, c) && f.validate (m);
}

bool
validate (
    Fulfillment const& f,
    Condition const& c)
{
    return validate (f, c, {});
}

}
}
