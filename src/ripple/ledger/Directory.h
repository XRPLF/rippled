//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_DIR_H_INCLUDED
#define RIPPLE_LEDGER_DIR_H_INCLUDED

#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class Dir
{
private:
    ReadView const* view_ = nullptr;
    Keylet root_;
    std::shared_ptr<SLE const> sle_;
    STVector256 const* indexes_ = nullptr;

public:
    class const_iterator;
    using value_type = std::shared_ptr<SLE const>;

    Dir(ReadView const&, Keylet const&);

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

class Dir::const_iterator
{
public:
    using value_type =
        Dir::value_type;
    using pointer =
        value_type const*;
    using reference =
        value_type const&;
    using difference_type =
        std::ptrdiff_t;
    using iterator_category =
        std::forward_iterator_tag;

    bool
    operator==(const_iterator const& other) const;

    bool
    operator!=(const_iterator const& other) const
    {
        return ! (*this == other);
    }

    reference
    operator*() const;

    pointer
    operator->() const
    {
        return &**this;
    }

    const_iterator&
    operator++();

    const_iterator
    operator++(int);

    Keylet const&
    page() const
    {
        return page_;
    }

    uint256
    index() const
    {
        return index_;
    }

private:
    friend class Dir;

    const_iterator(ReadView const& view,
        Keylet const& root, Keylet const& page)
    : view_(&view)
    , root_(root)
    , page_(page)
    {
    }

    ReadView const* view_ = nullptr;
    Keylet root_;
    Keylet page_;
    uint256 index_;
    boost::optional<value_type> mutable cache_;
    std::shared_ptr<SLE const> sle_;
    STVector256 const* indexes_ = nullptr;
    std::vector<uint256>::const_iterator it_;
};

} // ripple

#endif
