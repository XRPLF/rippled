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

#ifndef RIPPLE_RESOURCE_CHARGE_H_INCLUDED
#define RIPPLE_RESOURCE_CHARGE_H_INCLUDED

#include <ios>
#include <string>

namespace ripple {
namespace Resource {

/** A consumption charge. */
class Charge
{
public:
    /** The type used to hold a consumption charge. */
    using value_type = int;

    // A default constructed Charge has no way to get a label.  Delete
    Charge() = delete;

    /** Create a charge with the specified cost and name. */
    Charge(value_type cost, std::string const& label = std::string());

    /** Return the human readable label associated with the charge. */
    std::string const&
    label() const;

    /** Return the cost of the charge in Resource::Manager units. */
    value_type
    cost() const;

    /** Converts this charge into a human readable string. */
    std::string
    to_string() const;

    bool
    operator==(Charge const&) const;
    bool
    operator!=(Charge const&) const;

private:
    value_type m_cost;
    std::string m_label;
};

std::ostream&
operator<<(std::ostream& os, Charge const& v);

}  // namespace Resource
}  // namespace ripple

#endif
