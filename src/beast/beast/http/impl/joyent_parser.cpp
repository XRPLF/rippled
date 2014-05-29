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

#include <beast/http/impl/joyent_parser.h>

#include <beast/http/basic_message.h>

#include <boost/system/error_code.hpp>

namespace beast {
namespace joyent {

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4127) // conditional expression is constant
# pragma warning (disable: 4244) // integer conversion, possible loss of data
#endif
#include <beast/http/impl/http-parser/http_parser.c>
#ifdef _MSC_VER
# pragma warning (pop)
#endif

}
}

namespace boost {
namespace system {

template <>
struct is_error_code_enum <beast::joyent::http_errno>
    : std::true_type
{
};

template <>
struct is_error_condition_enum <beast::joyent::http_errno>
    : std::true_type
{
};

}
}

namespace beast {
namespace joyent {

http::method::methodc_t
convert_http_method (joyent::http_method m)
{
    switch (m)
    {
    case HTTP_DELETE:      return http::method::http_delete;
    case HTTP_GET:         return http::method::http_get;
    case HTTP_HEAD:        return http::method::http_head;
    case HTTP_POST:        return http::method::http_post;
    case HTTP_PUT:         return http::method::http_put;

    // pathological
    case HTTP_CONNECT:     return http::method::http_connect;
    case HTTP_OPTIONS:     return http::method::http_options;
    case HTTP_TRACE:       return http::method::http_trace;

    // webdav
    case HTTP_COPY:        return http::method::http_copy;
    case HTTP_LOCK:        return http::method::http_lock;
    case HTTP_MKCOL:       return http::method::http_mkcol;
    case HTTP_MOVE:        return http::method::http_move;
    case HTTP_PROPFIND:    return http::method::http_propfind;
    case HTTP_PROPPATCH:   return http::method::http_proppatch;
    case HTTP_SEARCH:      return http::method::http_search;
    case HTTP_UNLOCK:      return http::method::http_unlock;

    // subversion
    case HTTP_REPORT:      return http::method::http_report;
    case HTTP_MKACTIVITY:  return http::method::http_mkactivity;
    case HTTP_CHECKOUT:    return http::method::http_checkout;
    case HTTP_MERGE:       return http::method::http_merge;

    // upnp
    case HTTP_MSEARCH:     return http::method::http_msearch;
    case HTTP_NOTIFY:      return http::method::http_notify;
    case HTTP_SUBSCRIBE:   return http::method::http_subscribe;
    case HTTP_UNSUBSCRIBE: return http::method::http_unsubscribe;

    // RFC-5789
    case HTTP_PATCH:       return http::method::http_patch;
    case HTTP_PURGE:       return http::method::http_purge;
    };

    return http::method::http_get;
}

boost::system::error_code
convert_http_errno (joyent::http_errno err)
{
    class http_error_category_t
        : public boost::system::error_category
    {
    private:
        typedef boost::system::error_code error_code;
        typedef boost::system::error_condition error_condition;

    public:
        char const*
        name() const noexcept override
        {
            return "http_errno";
        }

        std::string
        message (int ev) const override
        {
            return joyent::http_errno_name (
                joyent::http_errno (ev));
        }

        error_condition
        default_error_condition (int ev) const noexcept override
        {
            return error_condition (ev, *this);
        }

        bool
        equivalent (int code, error_condition const& condition
            ) const noexcept override
        {
            return default_error_condition (code) == condition;
        }
        
        bool
        equivalent (error_code const& code, int condition
            ) const noexcept override
        {
            return *this == code.category() &&
                code.value() == condition;
        }
    };

    static http_error_category_t http_error_category;
    
    return boost::system::error_code (
        err, http_error_category);
}

}
}
