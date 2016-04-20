//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_ERROR_HPP
#define BEAST_HTTP_DETAIL_ERROR_HPP

#include <beast/http/impl/http_parser.h>
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
        return http_errno_description(
            static_cast<http_errno>(ev));
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
