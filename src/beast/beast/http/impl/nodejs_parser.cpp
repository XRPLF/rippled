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

#include <beast/http/impl/nodejs_parser.h>
#include <beast/http/method.h>
#include <boost/system/error_code.hpp>

namespace beast {
namespace nodejs {

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
struct is_error_code_enum <beast::nodejs::http_errno>
    : std::true_type
{
};

template <>
struct is_error_condition_enum <beast::nodejs::http_errno>
    : std::true_type
{
};

}
}

namespace beast {
namespace nodejs {

http::method_t
convert_http_method (nodejs::http_method m)
{
    switch (m)
    {
    case HTTP_DELETE:      return http::method_t::http_delete;
    case HTTP_GET:         return http::method_t::http_get;
    case HTTP_HEAD:        return http::method_t::http_head;
    case HTTP_POST:        return http::method_t::http_post;
    case HTTP_PUT:         return http::method_t::http_put;

    // pathological
    case HTTP_CONNECT:     return http::method_t::http_connect;
    case HTTP_OPTIONS:     return http::method_t::http_options;
    case HTTP_TRACE:       return http::method_t::http_trace;

    // webdav
    case HTTP_COPY:        return http::method_t::http_copy;
    case HTTP_LOCK:        return http::method_t::http_lock;
    case HTTP_MKCOL:       return http::method_t::http_mkcol;
    case HTTP_MOVE:        return http::method_t::http_move;
    case HTTP_PROPFIND:    return http::method_t::http_propfind;
    case HTTP_PROPPATCH:   return http::method_t::http_proppatch;
    case HTTP_SEARCH:      return http::method_t::http_search;
    case HTTP_UNLOCK:      return http::method_t::http_unlock;
    case HTTP_BIND:        return http::method_t::http_bind;
    case HTTP_REBIND:      return http::method_t::http_rebind;
    case HTTP_UNBIND:      return http::method_t::http_unbind;
    case HTTP_ACL:         return http::method_t::http_acl;

    // subversion
    case HTTP_REPORT:      return http::method_t::http_report;
    case HTTP_MKACTIVITY:  return http::method_t::http_mkactivity;
    case HTTP_CHECKOUT:    return http::method_t::http_checkout;
    case HTTP_MERGE:       return http::method_t::http_merge;

    // upnp
    case HTTP_MSEARCH:     return http::method_t::http_msearch;
    case HTTP_NOTIFY:      return http::method_t::http_notify;
    case HTTP_SUBSCRIBE:   return http::method_t::http_subscribe;
    case HTTP_UNSUBSCRIBE: return http::method_t::http_unsubscribe;

    // RFC-5789
    case HTTP_PATCH:       return http::method_t::http_patch;
    case HTTP_PURGE:       return http::method_t::http_purge;

    // CalDav
    case HTTP_MKCALENDAR:  return http::method_t::http_mkcalendar;

    // RFC-2068, section 19.6.1.2
    case HTTP_LINK:        return http::method_t::http_link;
    case HTTP_UNLINK:      return http::method_t::http_unlink;
    };

    return http::method_t::http_get;
}

} // nodejs
} // beast
