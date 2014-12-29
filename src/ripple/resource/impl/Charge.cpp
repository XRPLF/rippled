//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/resource/Charge.h>
#include <sstream>

namespace ripple {
namespace Resource {

Charge::Charge (value_type cost, std::string const& label)
    : m_cost (cost)
    , m_label (label)
{
}

std::string const& Charge::label () const
{
    return m_label;
}

Charge::value_type Charge::cost() const
{
    return m_cost;
}

std::string Charge::to_string () const
{
    std::stringstream ss;
    ss << m_label << " ($" << m_cost << ")";
    return ss.str();
}

std::ostream& operator<< (std::ostream& os, Charge const& v)
{
    os << v.to_string();
    return os;
}

bool Charge::operator== (Charge const& c) const
{
    return c.m_cost == m_cost;
}

bool Charge::operator!= (Charge const& c) const
{
    return c.m_cost != m_cost;
}

}
}
