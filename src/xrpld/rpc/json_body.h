#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/message.hpp>

namespace xrpl {

/// Body that holds JSON
struct JsonBody
{
    explicit JsonBody() = default;

    using value_type = json::Value;

    class reader  // NOLINT(readability-identifier-naming) -- Boost.Beast body concept name
    {
        using dynamic_buffer_type = boost::beast::multi_buffer;

        dynamic_buffer_type buffer_;

    public:
        using const_buffers_type = typename dynamic_buffer_type::const_buffers_type;

        using is_deferred = std::false_type;

        template <bool IsRequest, class Fields>
        explicit reader(boost::beast::http::message<IsRequest, JsonBody, Fields> const& m)
        {
            stream(m.body, [&](void const* data, std::size_t n) {
                buffer_.commit(
                    boost::asio::buffer_copy(buffer_.prepare(n), boost::asio::buffer(data, n)));
            });
        }

        void
        init(boost::beast::error_code&) noexcept
        {
        }

        // get() must return a boost::optional (not a std::optional) to meet
        // requirements of a boost::beast::BodyReader.
        boost::optional<std::pair<const_buffers_type, bool>>
        get(boost::beast::error_code& ec)
        {
            return {{buffer_.data(), false}};
        }

        void
        finish(boost::beast::error_code&)
        {
        }
    };

    class writer  // NOLINT(readability-identifier-naming) -- Boost.Beast body concept name
    {
        std::string bodyString_;

    public:
        using const_buffers_type = boost::asio::const_buffer;

        template <bool IsRequest, class Fields>
        explicit writer(
            boost::beast::http::header<IsRequest, Fields> const& fields,
            value_type const& value)
            : bodyString_(to_string(value))
        {
        }

        static void
        init(boost::beast::error_code& ec)
        {
            ec.assign(0, ec.category());
        }

        // get() must return a boost::optional (not a std::optional) to meet
        // requirements of a boost::beast::BodyWriter.
        boost::optional<std::pair<const_buffers_type, bool>>
        get(boost::beast::error_code& ec)
        {
            ec.assign(0, ec.category());
            return {{const_buffers_type{bodyString_.data(), bodyString_.size()}, false}};
        }
    };
};

}  // namespace xrpl
