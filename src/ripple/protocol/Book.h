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

#ifndef RIPPLE_PROTOCOL_BOOK_H_INCLUDED
#define RIPPLE_PROTOCOL_BOOK_H_INCLUDED

#include <ripple/protocol/Issue.h>
#include <boost/utility/base_from_member.hpp>

namespace ripple {

/** Specifies an order book.
    The order book is a pair of Issues called in and out.
    @see Issue.
*/
template <bool ByValue>
class BookType
{
public:
    using Issue = IssueType <ByValue>;

    Issue in;
    Issue out;

    BookType ()
    {
    }

    BookType (Issue const& in_, Issue const& out_)
        : in (in_)
        , out (out_)
    {
    }

    template <bool OtherByValue>
    BookType (BookType <OtherByValue> const& other)
        : in (other.in)
        , out (other.out)
    {
    }

    /** Assignment.
        This is only valid when ByValue == `true`
    */
    template <bool OtherByValue>
    BookType& operator= (BookType <OtherByValue> const& other)
    {
        in = other.in;
        out = other.out;
        return *this;
    }
};

template <bool ByValue>
bool isConsistent(BookType<ByValue> const& book)
{
    return isConsistent(book.in) && isConsistent (book.out)
            && book.in != book.out;
}

template <bool ByValue>
std::string to_string (BookType<ByValue> const& book)
{
    return to_string(book.in) + "->" + to_string(book.out);
}

template <bool ByValue>
std::ostream& operator<<(std::ostream& os, BookType<ByValue> const& x)
{
    os << to_string (x);
    return os;
}

template <bool ByValue, class Hasher>
void hash_append (Hasher& h, BookType<ByValue> const& b)
{
    using beast::hash_append;
    hash_append (h, b.in, b.out);
}

template <bool ByValue>
BookType<ByValue> reversed (BookType<ByValue> const& book)
{
    return BookType<ByValue> (book.out, book.in);
}

/** Ordered comparison. */
template <bool LhsByValue, bool RhsByValue>
int compare (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    int const diff (compare (lhs.in, rhs.in));
    if (diff != 0)
        return diff;
    return compare (lhs.out, rhs.out);
}

/** Equality comparison. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator== (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    return (lhs.in == rhs.in) &&
           (lhs.out == rhs.out);
}

template <bool LhsByValue, bool RhsByValue>
bool operator!= (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    return (lhs.in != rhs.in) ||
           (lhs.out != rhs.out);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
template <bool LhsByValue, bool RhsByValue>
bool operator< (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    int const diff (compare (lhs.in, rhs.in));
    if (diff != 0)
        return diff < 0;
    return lhs.out < rhs.out;
}

template <bool LhsByValue, bool RhsByValue>
bool operator> (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    return rhs < lhs;
}

template <bool LhsByValue, bool RhsByValue>
bool operator>= (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    return ! (lhs < rhs);
}

template <bool LhsByValue, bool RhsByValue>
bool operator<= (BookType <LhsByValue> const& lhs,
    BookType <RhsByValue> const& rhs)
{
    return ! (rhs < lhs);
}
/** @} */

//------------------------------------------------------------------------------

using Book = BookType <true>;
using BookRef = BookType <false>;

}

//------------------------------------------------------------------------------

namespace std {

template <bool ByValue>
struct hash <ripple::IssueType <ByValue>>
    : private boost::base_from_member <std::hash <ripple::Currency>, 0>
    , private boost::base_from_member <std::hash <ripple::AccountID>, 1>
{
private:
    using currency_hash_type = boost::base_from_member <
        std::hash <ripple::Currency>, 0>;
    using issuer_hash_type = boost::base_from_member <
        std::hash <ripple::AccountID>, 1>;

public:
    using value_type = std::size_t;
    using argument_type = ripple::IssueType <ByValue>;

    value_type operator() (argument_type const& value) const
    {
        value_type result (currency_hash_type::member (value.currency));
        if (!isXRP (value.currency))
            boost::hash_combine (result,
                issuer_hash_type::member (value.account));
        return result;
    }
};

//------------------------------------------------------------------------------

template <bool ByValue>
struct hash <ripple::BookType <ByValue>>
{
private:
    using hasher = std::hash <ripple::IssueType <ByValue>>;

    hasher m_hasher;

public:
    using value_type = std::size_t;
    using argument_type = ripple::BookType <ByValue>;

    value_type operator() (argument_type const& value) const
    {
        value_type result (m_hasher (value.in));
        boost::hash_combine (result, m_hasher (value.out));
        return result;
    }
};

}

//------------------------------------------------------------------------------

namespace boost {

template <bool ByValue>
struct hash <ripple::IssueType <ByValue>>
    : std::hash <ripple::IssueType <ByValue>>
{
    using Base = std::hash <ripple::IssueType <ByValue>>;
    // VFALCO NOTE broken in vs2012
    //using Base::Base; // inherit ctors
};

template <bool ByValue>
struct hash <ripple::BookType <ByValue>>
    : std::hash <ripple::BookType <ByValue>>
{
    using Base = std::hash <ripple::BookType <ByValue>>;
    // VFALCO NOTE broken in vs2012
    //using Base::Base; // inherit ctors
};

}

#endif
