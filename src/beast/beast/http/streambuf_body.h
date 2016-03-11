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

#ifndef BEAST_HTTP_STREAMBUF_BODY_H_INCLUDED
#define BEAST_HTTP_STREAMBUF_BODY_H_INCLUDED

#include <beast/http/error.h>
#include <beast/http/message.h>
#include <beast/asio/append_buffers.h>
#include <beast/asio/streambuf.h>
#include <memory>
#include <string>

namespace beast {
namespace http {

/** A Body represented by a Streambuf
*/
template<class Streambuf>
struct basic_streambuf_body
{
    using value_type = Streambuf;

    class reader
    {
        value_type& sb_;

    public:
        template<bool isRequest, class Allocator>
        explicit
        reader(message<isRequest,
                basic_streambuf_body, Allocator>& m) noexcept
            : sb_(m.body)
        {
        }

        void
        write(void const* data,
            std::size_t size, error_code&) noexcept
        {
            using boost::asio::buffer;
            using boost::asio::buffer_copy;
            sb_.commit(buffer_copy(
                sb_.prepare(size), buffer(data, size)));
        }
    };

    class writer
    {
        Streambuf const& body_;

    public:
        template<bool isRequest, class Allocator>
        explicit
        writer(message<isRequest, basic_streambuf_body,
                Allocator> const& m)
            : body_(m.body_)
        {
        }

        void
        init(error_code& ec)
        {
        }

        std::size_t
        content_length() const
        {
            return body_.size();
        }

        template<class Write>
        boost::tribool
        operator()(resume_context&&, error_code&, Write&& write)
        {
            write(body_.data());
            return true;
        }

        auto
        data() const noexcept
        {
            return body_.data();
        }
    };
};

using streambuf_body = basic_streambuf_body<streambuf>;

} // http
} // beast

#endif
