//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
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
    connection_closed = 1,

    bad_method,
    bad_uri,
    bad_version,
    bad_crlf,

    bad_status,
    bad_reason,

    bad_field,
    bad_value,
    bad_content_length,
    illegal_content_length,

    invalid_chunk_size,
    invalid_ext_name,
    invalid_ext_val,

    header_too_big,
    body_too_big,
    short_read
};

} // http
} // beast

#include <beast/http/impl/parse_error.ipp>

#endif
