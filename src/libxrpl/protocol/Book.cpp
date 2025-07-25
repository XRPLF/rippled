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

#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Issue.h>

#include <ostream>
#include <string>

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
    return Book(book.out, book.in, book.domain);
}

}  // namespace ripple
