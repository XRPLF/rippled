//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DYNAMIC_BODY_HPP
#define BEAST_HTTP_DYNAMIC_BODY_HPP

#include <beast/config.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <utility>

namespace beast {
namespace http {

/** A @b Body using a @b DynamicBuffer

    This body uses a @b DynamicBuffer as a memory-based container
    for holding message payloads. Messages using this body type
    may be serialized and parsed.
*/
template<class DynamicBuffer>
struct basic_dynamic_body
{
    static_assert(is_dynamic_buffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");

    /** The type of container used for the body

        This determines the type of @ref message::body
        when this body type is used with a message container.
    */
    using value_type = DynamicBuffer;

    /** Returns the payload size of the body

        When this body is used with @ref message::prepare_payload,
        the Content-Length will be set to the payload size, and
        any chunked Transfer-Encoding will be removed.
    */
    static
    std::uint64_t
    size(value_type const& v)
    {
        return v.size();
    }

    /** The algorithm for serializing the body

        Meets the requirements of @b BodyReader.
    */
#if BEAST_DOXYGEN
    using reader = implementation_defined;
#else
    class reader
    {
        DynamicBuffer const& body_;

    public:
        using const_buffers_type =
            typename DynamicBuffer::const_buffers_type;

        template<bool isRequest, class Fields>
        explicit
        reader(message<isRequest,
                basic_dynamic_body, Fields> const& m)
            : body_(m.body)
        {
        }

        void
        init(error_code& ec)
        {
            ec.assign(0, ec.category());
        }

        boost::optional<std::pair<const_buffers_type, bool>>
        get(error_code& ec)
        {
            ec.assign(0, ec.category());
            return {{body_.data(), false}};
        }
    };
#endif

    /** The algorithm for parsing the body

        Meets the requirements of @b BodyReader.
    */
#if BEAST_DOXYGEN
    using writer = implementation_defined;
#else
    class writer
    {
        value_type& body_;

    public:
        template<bool isRequest, class Fields>
        explicit
        writer(message<isRequest, basic_dynamic_body, Fields>& msg)
            : body_(msg.body)
        {
        }

        void
        init(boost::optional<std::uint64_t> const&, error_code& ec)
        {
            ec.assign(0, ec.category());
        }

        template<class ConstBufferSequence>
        std::size_t
        put(ConstBufferSequence const& buffers,
            error_code& ec)
        {
            using boost::asio::buffer_copy;
            using boost::asio::buffer_size;
            auto const n = buffer_size(buffers);
            if(body_.size() > body_.max_size() - n)
            {
                ec = error::buffer_overflow;
                return 0;
            }
            boost::optional<typename
                DynamicBuffer::mutable_buffers_type> b;
            try
            {
                b.emplace(body_.prepare((std::min)(n,
                    body_.max_size() - body_.size())));
            }
            catch(std::length_error const&)
            {
                ec = error::buffer_overflow;
                return 0;
            }
            ec.assign(0, ec.category());
            auto const bytes_transferred =
                buffer_copy(*b, buffers);
            body_.commit(bytes_transferred);
            return bytes_transferred;
        }

        void
        finish(error_code& ec)
        {
            ec.assign(0, ec.category());
        }
    };
#endif
};

/** A dynamic message body represented by a @ref multi_buffer

    Meets the requirements of @b Body.
*/
using dynamic_body = basic_dynamic_body<multi_buffer>;

} // http
} // beast

#endif
