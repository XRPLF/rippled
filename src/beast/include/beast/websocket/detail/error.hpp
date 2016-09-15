//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_ERROR_HPP
#define BEAST_WEBSOCKET_DETAIL_ERROR_HPP

#include <beast/websocket/error.hpp>

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::websocket::error>
{
    static bool const value = true;
};
} // system
} // boost

namespace beast {
namespace websocket {
namespace detail {

class error_category : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "websocket";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<error>(ev))
        {
        case error::closed: return "WebSocket connection closed normally";
        case error::failed: return "WebSocket connection failed due to a protocol violation";
        case error::handshake_failed: return "WebSocket Upgrade handshake failed";
        case error::keep_alive: return "WebSocket Upgrade handshake failed but connection is still open";

        case error::response_malformed: return "malformed HTTP response";
        case error::response_failed: return "upgrade request failed";
        case error::response_denied: return "upgrade request denied";
        case error::request_malformed: return "malformed HTTP request";
        case error::request_invalid: return "upgrade request invalid";
        case error::request_denied: return "upgrade request denied";
        default:
            return "websocket error";
        }
    }

    boost::system::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return boost::system::error_condition(ev, *this);
    }

    bool
    equivalent(int ev,
        boost::system::error_condition const& condition
            ) const noexcept override
    {
        return condition.value() == ev &&
            &condition.category() == this;
    }

    bool
    equivalent(error_code const& error, int ev) const noexcept override
    {
        return error.value() == ev &&
            &error.category() == this;
    }
};

inline
boost::system::error_category const&
get_error_category()
{
    static detail::error_category const cat{};
    return cat;
}

} // detail
} // websocket
} // beast

#endif
