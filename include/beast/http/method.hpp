//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_METHOD_HPP
#define BEAST_HTTP_METHOD_HPP

#include <cassert>
#include <memory>
#include <string>

namespace beast {
namespace http {

enum class method_t
{
    http_delete,
    http_get,
    http_head,
    http_post,
    http_put,

    // pathological
    http_connect,
    http_options,
    http_trace,

    // webdav
    http_copy,
    http_lock,
    http_mkcol,
    http_move,
    http_propfind,
    http_proppatch,
    http_search,
    http_unlock,
    http_bind,
    http_rebind,
    http_unbind,
    http_acl,

    // subversion
    http_report,
    http_mkactivity,
    http_checkout,
    http_merge,

    // upnp
    http_msearch,
    http_notify,
    http_subscribe,
    http_unsubscribe,

    // RFC-5789
    http_patch,
    http_purge,

    // CalDav
    http_mkcalendar,

    // RFC-2068, section 19.6.1.2
    http_link,
    http_unlink
};

template<class = void>
std::string
to_string(method_t m)
{
    switch(m)
    {
    case method_t::http_delete:       return "DELETE";
    case method_t::http_get:          return "GET";
    case method_t::http_head:         return "HEAD";
    case method_t::http_post:         return "POST";
    case method_t::http_put:          return "PUT";

    case method_t::http_connect:      return "CONNECT";
    case method_t::http_options:      return "OPTIONS";
    case method_t::http_trace:        return "TRACE";

    case method_t::http_copy:         return "COPY";
    case method_t::http_lock:         return "LOCK";
    case method_t::http_mkcol:        return "MKCOL";
    case method_t::http_move:         return "MOVE";
    case method_t::http_propfind:     return "PROPFIND";
    case method_t::http_proppatch:    return "PROPPATCH";
    case method_t::http_search:       return "SEARCH";
    case method_t::http_unlock:       return "UNLOCK";

    case method_t::http_report:       return "REPORT";
    case method_t::http_mkactivity:   return "MKACTIVITY";
    case method_t::http_checkout:     return "CHECKOUT";
    case method_t::http_merge:        return "MERGE";

    case method_t::http_msearch:      return "MSEARCH";
    case method_t::http_notify:       return "NOTIFY";
    case method_t::http_subscribe:    return "SUBSCRIBE";
    case method_t::http_unsubscribe:  return "UNSUBSCRIBE";

    case method_t::http_patch:        return "PATCH";
    case method_t::http_purge:        return "PURGE";

    default:
        assert(false);
        break;
    };

    return "GET";
}

template <class Stream>
Stream&
operator<< (Stream& s, method_t m)
{
    return s << to_string(m);
}

/** Returns the string corresponding to the numeric HTTP status code. */
template<class = void>
std::string
status_text (int status)
{
    switch(status)
    {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    //case 306: return "<reserved>";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default:
        break;
    }
    return "Unknown HTTP status";
}

} // http
} // beast

#endif
