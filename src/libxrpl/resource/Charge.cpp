#include <xrpl/resource/Charge.h>

#include <compare>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace xrpl::Resource {

Charge::Charge(value_type cost, std::string label) : cost_(cost), label_(std::move(label))
{
}

std::string const&
Charge::label() const
{
    return label_;
}

Charge::value_type
Charge::cost() const
{
    return cost_;
}

std::string
Charge::toString() const
{
    std::stringstream ss;
    ss << label_ << " ($" << cost_ << ")";
    return ss.str();
}

std::ostream&
operator<<(std::ostream& os, Charge const& v)
{
    os << v.toString();
    return os;
}

bool
Charge::operator==(Charge const& c) const
{
    return c.cost_ == cost_;
}

std::strong_ordering
Charge::operator<=>(Charge const& c) const
{
    return cost_ <=> c.cost_;
}

Charge
Charge::operator*(value_type m) const
{
    return Charge(cost_ * m, label_);
}

}  // namespace xrpl::Resource
