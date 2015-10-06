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

#include <ripple/protocol/Issue.h>

namespace ripple {

bool
isConsistent (Issue const& ac)
{
    return isXRP (ac.currency) == isXRP (ac.account);
}

std::string
to_string (Issue const& ac)
{
    if (isXRP (ac.account))
        return to_string (ac.currency);

    return to_string(ac.account) + "/" + to_string(ac.currency);
}

std::ostream&
operator<< (std::ostream& os, Issue const& x)
{
    os << to_string (x);
    return os;
}

/** Ordered comparison.
    The assets are ordered first by currency and then by account,
    if the currency is not XRP.
*/
int
compare (Issue const& lhs, Issue const& rhs)
{
    int diff = compare (lhs.currency, rhs.currency);
    if (diff != 0)
        return diff;
    if (isXRP (lhs.currency))
        return 0;
    return compare (lhs.account, rhs.account);
}

/** Equality comparison. */
/** @{ */
bool
operator== (Issue const& lhs, Issue const& rhs)
{
    return compare (lhs, rhs) == 0;
}

bool
operator!= (Issue const& lhs, Issue const& rhs)
{
    return ! (lhs == rhs);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
bool
operator< (Issue const& lhs, Issue const& rhs)
{
    return compare (lhs, rhs) < 0;
}

bool
operator> (Issue const& lhs, Issue const& rhs)
{
    return rhs < lhs;
}

bool
operator>= (Issue const& lhs, Issue const& rhs)
{
    return ! (lhs < rhs);
}

bool
operator<= (Issue const& lhs, Issue const& rhs)
{
    return ! (rhs < lhs);
}

} // ripple
