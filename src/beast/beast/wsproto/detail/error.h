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

#ifndef BEAST_WSPROTO_DETAIL_ERROR_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_ERROR_H_INCLUDED

#include <beast/wsproto/error.h>

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::wsproto::error>
{
    static bool const value = true;
};
} // system
} // boost

namespace beast {
namespace wsproto {
namespace detail {

class error_category : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "wsproto";
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
            return "wsproto.error";
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
} // wsproto
} // beast

#endif
