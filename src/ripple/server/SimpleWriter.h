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

#ifndef RIPPLE_SERVER_SIMPLEWRITER_H_INCLUDED
#define RIPPLE_SERVER_SIMPLEWRITER_H_INCLUDED

#include <ripple/server/Writer.h>
#include <beast/core/multi_buffer.hpp>
#include <beast/core/ostream.hpp>
#include <beast/http/message.hpp>
#include <beast/http/write.hpp>
#include <utility>

namespace ripple {

/// Deprecated: Writer that serializes a HTTP/1 message
class SimpleWriter : public Writer
{
    beast::multi_buffer sb_;

public:
    template<bool isRequest, class Body, class Headers>
    explicit
    SimpleWriter(beast::http::message<
        isRequest, Body, Headers> const& msg)
    {
        beast::ostream(sb_) << msg;
    }

    bool
    complete() override
    {
        return sb_.size() == 0;
    }

    void
    consume (std::size_t bytes) override
    {
        sb_.consume(bytes);
    }

    bool
    prepare(std::size_t bytes,
        std::function<void(void)>) override
    {
        return true;
    }

    std::vector<boost::asio::const_buffer>
    data() override
    {
        auto const& buf = sb_.data();
        std::vector<boost::asio::const_buffer> result;
        result.reserve(std::distance(buf.begin(), buf.end()));
        for (auto const& b : buf)
            result.push_back(b);
        return result;
    }
};

} // ripple

#endif
