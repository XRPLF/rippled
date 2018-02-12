//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_ERROR_IPP
#define BEAST_WEBSOCKET_IMPL_ERROR_IPP

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

class websocket_error_category : public error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "beast.websocket";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<error>(ev))
        {
        case error::closed: return "WebSocket connection closed normally";
        case error::failed: return "WebSocket connection failed due to a protocol violation";
        case error::handshake_failed: return "WebSocket Upgrade handshake failed";

        default:
            return "beast.websocket error";
        }
    }

    error_condition
    default_error_condition(int ev) const noexcept override
    {
        return error_condition(ev, *this);
    }

    bool
    equivalent(int ev,
        error_condition const& condition
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
error_category const&
get_error_category()
{
    static detail::websocket_error_category const cat{};
    return cat;
}

} // detail

inline
error_code
make_error_code(error e)
{
    return error_code(
        static_cast<std::underlying_type<error>::type>(e),
            detail::get_error_category());
}

} // websocket
} // beast

#endif
