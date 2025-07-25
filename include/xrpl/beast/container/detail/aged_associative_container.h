//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONTAINER_DETAIL_AGED_ASSOCIATIVE_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_DETAIL_AGED_ASSOCIATIVE_CONTAINER_H_INCLUDED

namespace beast {
namespace detail {

// Extracts the key portion of value
template <bool maybe_map>
struct aged_associative_container_extract_t
{
    explicit aged_associative_container_extract_t() = default;

    template <class Value>
    decltype(Value::first) const&
    operator()(Value const& value) const
    {
        return value.first;
    }
};

template <>
struct aged_associative_container_extract_t<false>
{
    explicit aged_associative_container_extract_t() = default;

    template <class Value>
    Value const&
    operator()(Value const& value) const
    {
        return value;
    }
};

}  // namespace detail
}  // namespace beast

#endif
