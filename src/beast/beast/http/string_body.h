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

#ifndef BEAST_HTTP_STRING_BODY_H_INCLUDED
#define BEAST_HTTP_STRING_BODY_H_INCLUDED

#include <beast/http/error.h>
#include <beast/http/message.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/streambuf.h>
#include <memory>
#include <string>

namespace beast {
namespace http2 {

/** A Body represented by a std::string.
*/
struct string_body
{
    using value_type = std::string;

    static bool constexpr is_simple = true;

    class reader
    {
        value_type& s_;

    public:
        template<bool isReq, class Allocator>
        explicit
        reader(message<isReq, string_body, Allocator>& m)
            : s_(m.body)
        {
        }

        void
        write(void const* data, std::size_t size)
        {
            auto const n = s_.size();
            s_.resize(n + size);
            std::memcpy(&s_[n], data, size);
        }
    };

    class writer
    {
        streambuf sb;
        boost::asio::const_buffers_1 cb;

    public:
        template<bool isReq, class Allocator>
        explicit
        writer(message<isReq, string_body, Allocator> const& m)
            : cb(boost::asio::buffer(m.body))
        {
            m.write_headers(sb);
        }

        auto
        data() const
        {
            return append_buffers(sb.data(), cb);
        }
    };

    template<bool isRequest, class Allocator>
    static
    void
    prepare(message<isRequest, string_body, Allocator>& m)
    {
        m.headers.replace("Content-Length", m.body.size());
        m.headers.replace("Content-Type", "application/text");
    }
};

} // http
} // beast

#endif
