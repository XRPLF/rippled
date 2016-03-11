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
        template<bool isReq, class Allocator>
        explicit
        reader(message<isReq, empty_body, Allocator>&)
        {
        }

        void
        write(void const*, std::size_t, error_code&)
        {
        }
    };

    class writer
    {
        streambuf sb;

    public:
        static bool constexpr is_single_pass = true;
    
        template<bool isReq, class Allocator>
        explicit
        writer(message<isReq, empty_body, Allocator> const& m)
        {
            m.write_headers(sb);
        }

        void
        init(error_code&)
        {
        }

        auto
        data() const
        {
            return sb.data();
        }
    };

    template<bool isRequest, class Allocator>
    static
    void
    prepare(prepared_message<isRequest, empty_body, Allocator>& msg)
    {
        msg.headers.replace("Content-Length", 0);
        msg.headers.erase("Content-Type"); // VFALCO this right?
    }

    template<class Allocator,
        class OtherBody, class OtherAllocator>
    static
    void
    prepare(prepared_message<false, empty_body, Allocator>& msg,
        parsed_message<true, OtherBody, OtherAllocator> const&)
    {
        prepare(msg);
    }
};

} // http
} // beast

#endif
