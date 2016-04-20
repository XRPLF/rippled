//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_IDENTITY_CODEC_H_INCLUDED
#define BEAST_NUDB_IDENTITY_CODEC_H_INCLUDED

#include <utility>

namespace beast {
namespace nudb {

/** Codec which maps input directly to output. */
class identity
{
public:
    template <class... Args>
    explicit
    identity(Args&&... args)
    {
    }

    char const*
    name() const
    {
        return "none";
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&&) const
    {
        return std::make_pair(in, in_size);
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&&) const
    {
        return std::make_pair(in, in_size);
    }
};

} // nudb
} // beast

#endif
