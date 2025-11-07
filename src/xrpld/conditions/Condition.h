#ifndef XRPL_CONDITIONS_CONDITION_H
#define XRPL_CONDITIONS_CONDITION_H

#include <xrpld/conditions/detail/utils.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>

#include <cstdint>
#include <set>

namespace ripple {
namespace cryptoconditions {

enum class Type : std::uint8_t {
    preimageSha256 = 0,
    prefixSha256 = 1,
    thresholdSha256 = 2,
    rsaSha256 = 3,
    ed25519Sha256 = 4
};

class Condition
{
public:
    /** The largest binary condition we support.

        @note This value will be increased in the future, but it
              must never decrease, as that could cause conditions
              that were previously considered valid to no longer
              be allowed.
    */
    static constexpr std::size_t maxSerializedCondition = 128;

    /** Load a condition from its binary form

        @param s The buffer containing the fulfillment to load.
        @param ec Set to the error, if any occurred.

        The binary format for a condition is specified in the
        cryptoconditions RFC. See:

        https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#section-7.2
    */
    static std::unique_ptr<Condition>
    deserialize(Slice s, std::error_code& ec);

public:
    Type type;

    /** An identifier for this condition.

        This fingerprint is meant to be unique only with
        respect to other conditions of the same type.
    */
    Buffer fingerprint;

    /** The cost associated with this condition. */
    std::uint32_t cost;

    /** For compound conditions, set of conditions includes */
    std::set<Type> subtypes;

    Condition(Type t, std::uint32_t c, Slice fp)
        : type(t), fingerprint(fp), cost(c)
    {
    }

    Condition(Type t, std::uint32_t c, Buffer&& fp)
        : type(t), fingerprint(std::move(fp)), cost(c)
    {
    }

    ~Condition() = default;

    Condition(Condition const&) = default;
    Condition(Condition&&) = default;

    Condition() = delete;
};

inline bool
operator==(Condition const& lhs, Condition const& rhs)
{
    return lhs.type == rhs.type && lhs.cost == rhs.cost &&
        lhs.subtypes == rhs.subtypes && lhs.fingerprint == rhs.fingerprint;
}

inline bool
operator!=(Condition const& lhs, Condition const& rhs)
{
    return !(lhs == rhs);
}

}  // namespace cryptoconditions

}  // namespace ripple

#endif
