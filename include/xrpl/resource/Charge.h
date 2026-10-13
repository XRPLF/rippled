#pragma once

#include <compare>
#include <ostream>
#include <string>

namespace xrpl::resource {

/**
 * A consumption charge.
 */
class Charge
{
public:
    /**
     * The type used to hold a consumption charge.
     */
    using ValueType = int;

    // A default constructed Charge has no way to get a label.  Delete
    Charge() = delete;

    /**
     * Create a charge with the specified cost and name.
     */
    Charge(ValueType cost, std::string label = std::string());

    /**
     * Return the human readable label associated with the charge.
     */
    [[nodiscard]] std::string const&
    label() const;

    /**
     * Return the cost of the charge in resource::Manager units.
     */
    [[nodiscard]] ValueType
    cost() const;

    /**
     * Converts this charge into a human readable string.
     */
    [[nodiscard]] std::string
    toString() const;

    bool
    operator==(Charge const&) const;

    std::strong_ordering
    operator<=>(Charge const&) const;

    Charge
    operator*(ValueType m) const;

private:
    ValueType cost_;
    std::string label_;
};

std::ostream&
operator<<(std::ostream& os, Charge const& v);

}  // namespace xrpl::resource
