//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_EMPTY_BODY_HPP
#define BEAST_HTTP_EMPTY_BODY_HPP

#include <beast/core/error.hpp>
#include <beast/http/message.hpp>
#include <beast/http/resume_context.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

/** An empty content-body.

    Meets the requirements of @b `Body`.
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

    struct writer
    {
        template<bool isRequest, class Fields>
        explicit
        writer(message<isRequest, empty_body, Fields> const& m) noexcept
        {
            beast::detail::ignore_unused(m);
        }

        void
        init(error_code& ec) noexcept
        {
            beast::detail::ignore_unused(ec);
        }

        std::uint64_t
        content_length() const noexcept
        {
            return 0;
        }

        template<class WriteFunction>
        boost::tribool
        write(resume_context&&, error_code&,
            WriteFunction&& wf) noexcept
        {
            wf(boost::asio::null_buffers{});
            return true;
        }
    };
};

} // http
} // beast

#endif
