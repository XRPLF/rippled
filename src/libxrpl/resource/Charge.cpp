#include <xrpl/resource/Charge.h>

#include <compare>
#include <ostream>
#include <sstream>
#include <string>

namespace ripple {
namespace Resource {

Charge::Charge(value_type cost, std::string const& label)
    : m_cost(cost), m_label(label)
{
}

std::string const&
Charge::label() const
{
    return m_label;
}

Charge::value_type
Charge::cost() const
{
    return m_cost;
}

std::string
Charge::to_string() const
{
    std::stringstream ss;
    ss << m_label << " ($" << m_cost << ")";
    return ss.str();
}

std::ostream&
operator<<(std::ostream& os, Charge const& v)
{
    os << v.to_string();
    return os;
}

bool
Charge::operator==(Charge const& c) const
{
    return c.m_cost == m_cost;
}

std::strong_ordering
Charge::operator<=>(Charge const& c) const
{
    return m_cost <=> c.m_cost;
}

Charge
Charge::operator*(value_type m) const
{
    return Charge(m_cost * m, m_label);
}

}  // namespace Resource
}  // namespace ripple
