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
#include <beast/asio/streambuf.h>
#include <beast/http/message.h>
#include <utility>

namespace ripple {
namespace HTTP {

/** Writer that sends a simple HTTP response with a message body. */
class SimpleWriter : public Writer
{
private:
    beast::http::message message_;
    beast::asio::streambuf streambuf_;
    std::string body_;
    bool prepared_ = false;

public:
    explicit
    SimpleWriter(beast::http::message&& message)
        : message_(std::forward<beast::http::message>(message))
    {
    }

    beast::http::message&
    message()
    {
        return message_;
    }

    bool
    complete() override
    {
        return streambuf_.size() == 0;
    }

    void
    consume (std::size_t bytes) override
    {
        streambuf_.consume(bytes);
    }

    bool
    prepare (std::size_t bytes,
        std::function<void(void)>) override
    {
        if (! prepared_)
            do_prepare();
        return true;
    }

    std::vector<boost::asio::const_buffer>
    data() override
    {
        auto const& buf = streambuf_.data();
        std::vector<boost::asio::const_buffer> result;
        result.reserve(std::distance(buf.begin(), buf.end()));
        for (auto const& b : buf)
            result.push_back(b);
        return result;
    }

    /** Set the content body. */
    void
    body (std::string const& s)
    {
        body_ = s;
    }

private:
    void
    do_prepare()
    {
        prepared_ = true;
        message_.headers.erase("Content-Length");
        message_.headers.append("Content-Length",
            std::to_string(body_.size()));
        write(streambuf_, message_);
        write(streambuf_, body_;
    }
};

}
}

#endif
