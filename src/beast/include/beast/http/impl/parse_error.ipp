//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_PARSE_ERROR_IPP
#define BEAST_HTTP_IMPL_PARSE_ERROR_IPP

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::http::parse_error>
{
    static bool const value = true;
};
} // system
} // boost

namespace beast {
namespace http {
namespace detail {

class parse_error_category : public error_category
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
        case parse_error::connection_closed: return "data after Connection close";
        case parse_error::bad_method: return "bad method";
        case parse_error::bad_uri: return "bad request-target";
        case parse_error::bad_version: return "bad HTTP-Version";
        case parse_error::bad_crlf: return "missing CRLF";
        case parse_error::bad_status: return "bad status-code";
        case parse_error::bad_reason: return "bad reason-phrase";
        case parse_error::bad_field: return "bad field token";
        case parse_error::bad_value: return "bad field-value";
        case parse_error::bad_content_length: return "bad Content-Length";
        case parse_error::illegal_content_length: return "illegal Content-Length with chunked Transfer-Encoding";
        case parse_error::invalid_chunk_size: return "invalid chunk size";
        case parse_error::invalid_ext_name: return "invalid ext name";
        case parse_error::invalid_ext_val: return "invalid ext val";
        case parse_error::header_too_big: return "header size limit exceeded";
        case parse_error::body_too_big: return "body size limit exceeded";
        default:
        case parse_error::short_read: return "unexpected end of data";
        }
    }

    error_condition
    default_error_condition(int ev) const noexcept override
    {
        return error_condition{ev, *this};
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
get_parse_error_category()
{
    static parse_error_category const cat{};
    return cat;
}

} // detail

inline
error_code
make_error_code(parse_error ev)
{
    return error_code{
        static_cast<std::underlying_type<parse_error>::type>(ev),
            detail::get_parse_error_category()};
}

} // http
} // beast

#endif
