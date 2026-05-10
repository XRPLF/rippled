#include <xrpl/resource/Charge.h>
#include <xrpl/basics/TraceLog.h>

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
    TRACE_FUNC();
    return label_;
}

Charge::value_type
Charge::cost() const
{
    TRACE_FUNC();
    return cost_;
}

std::string
Charge::toString() const
{
    TRACE_FUNC();
    std::stringstream ss;
    ss << label_ << " ($" << cost_ << ")";
    return ss.str();
}

std::ostream&
operator<<(std::ostream& os, Charge const& v)
{
    TRACE_FUNC();
    os << v.toString();
    return os;
}

bool
Charge::operator==(Charge const& c) const
{
    TRACE_FUNC();
    return c.cost_ == cost_;
}

std::strong_ordering
Charge::operator<=>(Charge const& c) const
{
    TRACE_FUNC();
    return cost_ <=> c.cost_;
}

Charge
Charge::operator*(value_type m) const
{
    TRACE_FUNC();
    return Charge(cost_ * m, label_);
}

}  // namespace xrpl::Resource
