//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_EMPTY_BODY_HPP
#define BEAST_HTTP_EMPTY_BODY_HPP

#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
#include <beast/streambuf.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

/** An empty content-body.
*/
struct empty_body
{
#if GENERATING_DOCS
    /// The type of the `message::body` member
    using value_type = void;
#else
    struct value_type {};
#endif

#if GENERATING_DOCS
private:
#endif

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

        std::uint64_t
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
