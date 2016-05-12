//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSE_ERROR_HPP
#define BEAST_HTTP_PARSE_ERROR_HPP

#include <beast/core/error.hpp>

namespace beast {
namespace http {

enum class parse_error
{
    connection_closed,

    bad_method,
    bad_uri,
    bad_version,
    bad_crlf,
    bad_request,

    bad_status_code,
    bad_status,

    bad_field,
    bad_value,
    bad_content_length,
    illegal_content_length,
    bad_on_headers_rv,

    invalid_chunk_size,

    short_read,

    general
};

class parse_error_category : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "http";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<parse_error>(ev))
        {
        case parse_error::connection_closed:
            return "data after Connection close";

        case parse_error::bad_method:
            return "bad method";

        case parse_error::bad_uri:
            return "bad Request-URI";

        case parse_error::bad_version:
            return "bad HTTP-Version";

        case parse_error::bad_crlf:
            return "missing CRLF";

        case parse_error::bad_request:
            return "bad Request-Line";

        case parse_error::bad_status_code:
            return "bad Status-Code";

        case parse_error::bad_status:
            return "bad Status-Line";

        case parse_error::bad_field:
            return "bad field token";

        case parse_error::bad_value:
            return "bad field-value";

        case parse_error::bad_content_length:
            return "bad Content-Length";

        case parse_error::illegal_content_length:
            return "illegal Content-Length with chunked Transfer-Encoding";

        case parse_error::bad_on_headers_rv:
            return "on_headers returned an unknown value";

        case parse_error::invalid_chunk_size:
            return "invalid chunk size";

        case parse_error::short_read:
            return "unexpected end of data";

        default:
            return "parse error";
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
get_parse_error_category()
{
    static parse_error_category const cat{};
    return cat;
}

inline
boost::system::error_code
make_error_code(parse_error ev)
{
    return error_code(static_cast<int>(ev),
        get_parse_error_category());
}

} // http
} // beast

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::http::parse_error>
{
    static bool const value = true;
};
} // system
} // boost

#endif
