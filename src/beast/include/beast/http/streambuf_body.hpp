//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_STREAMBUF_BODY_HPP
#define BEAST_HTTP_STREAMBUF_BODY_HPP

#include <beast/http/body_type.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/streambuf.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

/** A message body represented by a Streambuf

    Meets the requirements of @b `Body`.
*/
template<class Streambuf>
struct basic_streambuf_body
{
    /// The type of the `message::body` member
    using value_type = Streambuf;

#if GENERATING_DOCS
private:
#endif

    class reader
    {
        value_type& sb_;

    public:
        template<bool isRequest, class Headers>
        explicit
        reader(message<isRequest,
                basic_streambuf_body, Headers>& m) noexcept
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
        writer(writer const&) = delete;
        writer& operator=(writer const&) = delete;

        template<bool isRequest, class Headers>
        explicit
        writer(message<
                isRequest, basic_streambuf_body, Headers> const& m)
            : body_(m.body)
        {
        }

        void
        init(error_code& ec)
        {
        }

        std::uint64_t
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
    };
};

using streambuf_body = basic_streambuf_body<streambuf>;

} // http
} // beast

#endif
