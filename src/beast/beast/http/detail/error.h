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

#ifndef BEAST_HTTP_DETAIL_ERROR_H_INCLUDED
#define BEAST_HTTP_DETAIL_ERROR_H_INCLUDED

#include <beast/http/impl/nodejs_parser.h>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {
namespace detail {

class message_category
    : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "http error";
    }

    std::string
    message(int ev) const override
    {
        return nodejs::http_errno_description(
            static_cast<nodejs::http_errno>(ev));
    }

    boost::system::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return boost::system::error_condition{ev, *this};
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
    equivalent(boost::system::error_code const& error,
        int ev) const noexcept override
    {
        return error.value() == ev &&
            &error.category() == this;
    }
};

template<class = void>
auto
make_error(int http_errno)
{
    static message_category const mc{};
    return boost::system::error_code{http_errno, mc};
}

} // detail
} // http
} // beast

#endif
