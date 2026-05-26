#include <xrpl/conditions/Condition.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/conditions/detail/PreimageSha256.h>
#include <xrpl/conditions/detail/error.h>
#include <xrpl/conditions/detail/utils.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>
#include <utility>

namespace xrpl::cryptoconditions {

namespace detail {
// The binary encoding of conditions differs based on their
// type.  All types define at least a fingerprint and cost
// sub-field.  Some types, such as the compound condition
// types, define additional sub-fields that are required to
// convey essential properties of the cryptocondition (such
// as the sub-types used by sub-conditions in the case of
// the compound types).
//
// Conditions are encoded as follows:
//
//    Condition ::= CHOICE {
//      preimageSha256   [0] SimpleSha256Condition,
//      prefixSha256     [1] CompoundSha256Condition,
//      thresholdSha256  [2] CompoundSha256Condition,
//      rsaSha256        [3] SimpleSha256Condition,
//      ed25519Sha256    [4] SimpleSha256Condition
//    }
//
//    SimpleSha256Condition ::= SEQUENCE {
//      fingerprint          OCTET STRING (SIZE(32)),
//      cost                 INTEGER (0..4294967295)
//    }
//
//    CompoundSha256Condition ::= SEQUENCE {
//      fingerprint          OCTET STRING (SIZE(32)),
//      cost                 INTEGER (0..4294967295),
//      subtypes             ConditionTypes
//    }
//
//    ConditionTypes ::= BIT STRING {
//      preImageSha256   (0),
//      prefixSha256     (1),
//      thresholdSha256  (2),
//      rsaSha256        (3),
//      ed25519Sha256    (4)
//    }

constexpr std::size_t kFingerprintSize = 32;

std::unique_ptr<Condition>
loadSimpleSha256(Type type, Slice s, std::error_code& ec)
{
    using namespace der;

    auto p = parsePreamble(s, ec);

    if (ec)
        return {};

    if (!isPrimitive(p) || !isContextSpecific(p))
    {
        ec = Error::IncorrectEncoding;
        return {};
    }

    if (p.tag != 0)
    {
        ec = Error::UnexpectedTag;
        return {};
    }

    if (p.length != kFingerprintSize)
    {
        ec = Error::FingerprintSize;
        return {};
    }

    Buffer b = parseOctetString(s, p.length, ec);

    if (ec)
        return {};

    p = parsePreamble(s, ec);

    if (ec)
        return {};

    if (!isPrimitive(p) || !isContextSpecific(p))
    {
        ec = Error::MalformedEncoding;
        return {};
    }

    if (p.tag != 1)
    {
        ec = Error::UnexpectedTag;
        return {};
    }

    auto cost = parseInteger<std::uint32_t>(s, p.length, ec);

    if (ec)
        return {};

    if (!s.empty())
    {
        ec = Error::TrailingGarbage;
        return {};
    }

    switch (type)
    {
        case Type::PreimageSha256:
            if (cost > PreimageSha256::kMaxPreimageLength)
            {
                ec = Error::PreimageTooLong;
                return {};
            }
            break;

        default:
            break;
    }

    return std::make_unique<Condition>(type, cost, std::move(b));
}

}  // namespace detail

std::unique_ptr<Condition>
Condition::deserialize(Slice s, std::error_code& ec)
{
    // Per the RFC, in a condition we choose a type based
    // on the tag of the item we contain:
    //
    // Condition ::= CHOICE {
    //     preimageSha256   [0] SimpleSha256Condition,
    //     prefixSha256     [1] CompoundSha256Condition,
    //     thresholdSha256  [2] CompoundSha256Condition,
    //     rsaSha256        [3] SimpleSha256Condition,
    //     ed25519Sha256    [4] SimpleSha256Condition
    // }
    if (s.empty())
    {
        ec = Error::BufferEmpty;
        return {};
    }

    using namespace der;

    auto const p = parsePreamble(s, ec);
    if (ec)
        return {};

    // All fulfillments are context-specific, constructed
    // types
    if (!isConstructed(p) || !isContextSpecific(p))
    {
        ec = Error::MalformedEncoding;
        return {};
    }

    if (p.length > s.size())
    {
        ec = Error::BufferUnderfull;
        return {};
    }

    if (s.size() > kMaxSerializedCondition)
    {
        ec = Error::LargeSize;
        return {};
    }

    std::unique_ptr<Condition> c;

    switch (p.tag)
    {
        case 0:  // PreimageSha256
            c = detail::loadSimpleSha256(Type::PreimageSha256, Slice(s.data(), p.length), ec);
            if (!ec)
                s += p.length;
            break;

        case 1:  // PrefixSha256
            ec = Error::UnsupportedType;
            return {};

        case 2:  // ThresholdSha256
            ec = Error::UnsupportedType;
            return {};

        case 3:  // RsaSha256
            ec = Error::UnsupportedType;
            return {};

        case 4:  // Ed25519Sha256
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

    return c;
}

}  // namespace xrpl::cryptoconditions
