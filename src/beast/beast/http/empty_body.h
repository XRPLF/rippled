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

#ifndef BEAST_HTTP_EMPTY_BODY_H_INCLUDED
#define BEAST_HTTP_EMPTY_BODY_H_INCLUDED

#include <beast/http/error.h>
#include <beast/http/message.h>
#include <beast/asio/streambuf.h>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

/** An empty content-body.
*/
struct empty_body
{
    struct value_type
    {
    };

    struct reader
    {
        template<bool isRequest, class Allocator>
        explicit
        reader(message<isRequest, empty_body, Allocator>&)
        {
        }

        void
        write(void const*, std::size_t, error_code&)
        {
        }
    };

    struct writer
    {
        template<bool isRequest, class Allocator>
        explicit
        writer(message<isRequest, empty_body, Allocator> const& m)
        {
        }

        void
        init(error_code& ec)
        {
        }

        std::size_t
        content_length() const
        {
            return 0;
        }

        template<class Write>
        boost::tribool
        operator()(resume_context&&, error_code&, Write&& write)
        {
            write(boost::asio::null_buffers{});
            return true;
        }
    };
};

} // http
} // beast

#endif
