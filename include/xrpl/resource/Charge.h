#pragma once

#include <compare>
#include <ostream>
#include <string>

namespace xrpl::Resource {

/** A consumption charge. */
class Charge
{
public:
    /** The type used to hold a consumption charge. */
    using value_type = int;

    // A default constructed Charge has no way to get a label.  Delete
    Charge() = delete;

    /** Create a charge with the specified cost and name. */
    Charge(value_type cost, std::string label = std::string());

    /** Return the human readable label associated with the charge. */
    [[nodiscard]] std::string const&
    label() const;

    /** Return the cost of the charge in Resource::Manager units. */
    [[nodiscard]] value_type
    cost() const;

    /** Converts this charge into a human readable string. */
    [[nodiscard]] std::string
    toString() const;

    bool
    operator==(Charge const&) const;

    std::strong_ordering
    operator<=>(Charge const&) const;

    Charge
    operator*(value_type m) const;

private:
    value_type cost_;
    std::string label_;
};

std::ostream&
operator<<(std::ostream& os, Charge const& v);

}  // namespace xrpl::Resource
