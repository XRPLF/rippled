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

#include <ripple/protocol/Book.h>

namespace ripple {

bool
isConsistent(Book const& book)
{
    return isConsistent(book.in) && isConsistent(book.out) &&
        book.in != book.out;
}

std::string
to_string(Book const& book)
{
    return to_string(book.in) + "->" + to_string(book.out);
}

std::ostream&
operator<<(std::ostream& os, Book const& x)
{
    os << to_string(x);
    return os;
}

Book
reversed(Book const& book)
{
    return Book(book.out, book.in);
}

/** Ordered comparison. */
int
compare(Book const& lhs, Book const& rhs)
{
    int const diff(compare(lhs.in, rhs.in));
    if (diff != 0)
        return diff;
    return compare(lhs.out, rhs.out);
}

/** Equality comparison. */
/** @{ */
bool
operator==(Book const& lhs, Book const& rhs)
{
    return (lhs.in == rhs.in) && (lhs.out == rhs.out);
}

bool
operator!=(Book const& lhs, Book const& rhs)
{
    return (lhs.in != rhs.in) || (lhs.out != rhs.out);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
bool
operator<(Book const& lhs, Book const& rhs)
{
    int const diff(compare(lhs.in, rhs.in));
    if (diff != 0)
        return diff < 0;
    return lhs.out < rhs.out;
}

bool
operator>(Book const& lhs, Book const& rhs)
{
    return rhs < lhs;
}

bool
operator>=(Book const& lhs, Book const& rhs)
{
    return !(lhs < rhs);
}

bool
operator<=(Book const& lhs, Book const& rhs)
{
    return !(rhs < lhs);
}

}  // namespace ripple
