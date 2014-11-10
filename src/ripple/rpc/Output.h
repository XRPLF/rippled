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

#ifndef RIPPLED_RIPPLE_BASICS_TYPES_OUTPUT_H
#define RIPPLED_RIPPLE_BASICS_TYPES_OUTPUT_H

namespace ripple {
namespace RPC {

struct Bytes
{
    char const* data = nullptr;
    std::size_t size = 0;

    Bytes (std::string const& s) : data (s.data()), size (s.size())
    {
    }

    Bytes (char const* cstr) : data (cstr), size (strlen (cstr))
    {
    }

    Bytes (char const& cstr) : data (&cstr), size (1)
    {
    }

    Bytes (char const* cstr, std::size_t s) : data (cstr), size (s)
    {
    }

    void appendTo (std::string& s) const
    {
        s.append (data, size);
    }
};

using Output = std::function <void (Bytes const&)>;

inline
Output stringOutput (std::string& s)
{
    return [&](Bytes const& b) { b.appendTo (s); };
}

} // RPC
} // ripple

#endif
