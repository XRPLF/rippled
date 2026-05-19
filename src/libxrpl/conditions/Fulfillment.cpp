#include <xrpl/conditions/Fulfillment.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/conditions/Condition.h>
#include <xrpl/conditions/detail/PreimageSha256.h>
#include <xrpl/conditions/detail/error.h>
#include <xrpl/conditions/detail/utils.h>

#include <memory>
#include <system_error>

namespace xrpl::cryptoconditions {

bool
match(Fulfillment const& f, Condition const& c)
{
    // Fast check: the fulfillment's type must match the
    // conditions's type:
    if (f.type() != c.type)
        return false;

    // Derive the condition from the given fulfillment
    // and ensure that it matches the given condition.
    return c == f.condition();
}

bool
validate(Fulfillment const& f, Condition const& c, Slice m)
{
    return match(f, c) && f.validate(m);
}

bool
validate(Fulfillment const& f, Condition const& c)
{
    return validate(f, c, {});
}

std::unique_ptr<Fulfillment>
Fulfillment::deserialize(Slice s, std::error_code& ec)
{
    // Per the RFC, in a fulfillment we choose a type based
    // on the tag of the item we contain:
    //
    // Fulfillment ::= CHOICE {
    //     preimageSha256   [0] PreimageFulfillment ,
    //     prefixSha256     [1] PrefixFulfillment,
    //     thresholdSha256  [2] ThresholdFulfillment,
    //     rsaSha256        [3] RsaSha256Fulfillment,
    //     ed25519Sha256    [4] Ed25519Sha512Fulfillment
    // }

    if (s.empty())
    {
        ec = Error::BufferEmpty;
        return nullptr;
    }

    using namespace der;

    auto const p = parsePreamble(s, ec);
    if (ec)
        return nullptr;

    // All fulfillments are context-specific, constructed types
    if (!isConstructed(p) || !isContextSpecific(p))
    {
        ec = Error::MalformedEncoding;
        return nullptr;
    }

    if (p.length > s.size())
    {
        ec = Error::BufferUnderfull;
        return {};
    }

    if (p.length < s.size())
    {
        ec = Error::BufferOverfull;
        return {};
    }

    if (p.length > kMaxSerializedFulfillment)
    {
        ec = Error::LargeSize;
        return {};
    }

    std::unique_ptr<Fulfillment> f;

    using TagType = decltype(p.tag);
    switch (p.tag)
    {
        case safeCast<TagType>(Type::PreimageSha256):
            f = PreimageSha256::deserialize(Slice(s.data(), p.length), ec);
            if (ec)
                return {};
            s += p.length;
            break;

        case safeCast<TagType>(Type::PrefixSha256):
            ec = Error::UnsupportedType;
            return {};
            break;

        case safeCast<TagType>(Type::ThresholdSha256):
            ec = Error::UnsupportedType;
            return {};
            break;

        case safeCast<TagType>(Type::RsaSha256):
            ec = Error::UnsupportedType;
            return {};
            break;

        case safeCast<TagType>(Type::Ed25519Sha256):
            ec = Error::UnsupportedType;
            return {};

        default:
            ec = Error::UnknownType;
            return {};
    }

    if (!s.empty())
    {
        ec = Error::TrailingGarbage;
        return {};
    }

    return f;
}

}  // namespace xrpl::cryptoconditions
