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

#ifndef BEAST_WSPROTO_ERROR_H_INCLUDED
#define BEAST_WSPROTO_ERROR_H_INCLUDED

#include <boost/system/error_code.hpp>

namespace beast {
namespace wsproto {

using error_code = boost::system::error_code;

enum class error : int
{
    /// Upgrade request denied for invalid fields
    bad_upgrade_request = 1,

    /// Upgrade request denied due to permissions
    upgrade_request_denied,

    /// Frame header invalid
    frame_header_invalid
};

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
        case error::bad_upgrade_request:
            return "bad Upgrade request";
        case error::upgrade_request_denied:
            return "upgrade request denied";
        case error::frame_header_invalid:
            return "frame header invalid";
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

} // detail

inline
boost::system::error_category const&
get_error_category()
{
    static detail::error_category const cat{};
    return cat;
}

inline
error_code
make_error_code(error e)
{
    return error_code(
        static_cast<int>(e), get_error_category());
}

} // wsproto
} // beast

namespace boost {
namespace system {
template<> struct is_error_code_enum<
    beast::wsproto::error>
        { static bool const value = true; };
} // system
} // boost

#endif
