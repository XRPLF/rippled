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

#ifndef BEAST_HTTP_FIELDS_H_INCLUDED
#define BEAST_HTTP_FIELDS_H_INCLUDED

#include <array>

namespace beast {
namespace http {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif  // defined(__clang__)

template<class = void>
auto const&
common_fields()
{
    // Must be sorted
    static std::array<char const*, 82> constexpr h{
         "Accept"
        ,"Accept-Charset"
        ,"Accept-Datetime"
        ,"Accept-Encoding"
        ,"Accept-Language"
        ,"Accept-Ranges"
        ,"Access-Control-Allow-Credentials"
        ,"Access-Control-Allow-Headers"
        ,"Access-Control-Allow-Methods"
        ,"Access-Control-Allow-Origin"
        ,"Access-Control-Expose-Headers"
        ,"Access-Control-Max-Age"
        ,"Access-Control-Request-Headers"
        ,"Access-Control-Request-Method"
        ,"Age"
        ,"Allow"
        ,"Authorization"
        ,"Cache-Control"
        ,"Connection"
        ,"Content-Disposition"
        ,"Content-Encoding"
        ,"Content-Language"
        ,"Content-Length"
        ,"Content-Location"
        ,"Content-MD5"
        ,"Content-Range"
        ,"Content-Type"
        ,"Cookie"
        ,"DNT"
        ,"Date"
        ,"ETag"
        ,"Expect"
        ,"Expires"
        ,"From"
        ,"Front-End-Https"
        ,"Host"
        ,"If-Match"
        ,"If-Modified-Since"
        ,"If-None-Match"
        ,"If-Range"
        ,"If-Unmodified-Since"
        ,"Keep-Alive"
        ,"Last-Modified"
        ,"Link"
        ,"Location"
        ,"Max-Forwards"
        ,"Origin"
        ,"P3P"
        ,"Pragma"
        ,"Proxy-Authenticate"
        ,"Proxy-Authorization"
        ,"Proxy-Connection"
        ,"Range"
        ,"Referer"
        ,"Refresh"
        ,"Retry-After"
        ,"Server"
        ,"Set-Cookie"
        ,"Strict-Transport-Security"
        ,"TE"
        ,"Timestamp"
        ,"Trailer"
        ,"Transfer-Encoding"
        ,"Upgrade"
        ,"User-Agent"
        ,"VIP"
        ,"Vary"
        ,"Via"
        ,"WWW-Authenticate"
        ,"Warning"
        ,"X-Accel-Redirect"
        ,"X-Content-Security-Policy-Report-Only"
        ,"X-Content-Type-Options"
        ,"X-Forwarded-For"
        ,"X-Forwarded-Proto"
        ,"X-Frame-Options"
        ,"X-Powered-By"
        ,"X-Real-IP"
        ,"X-Requested-With"
        ,"X-UA-Compatible"
        ,"X-Wap-Profile"
        ,"X-XSS-Protection"
    };

    return h;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif  // defined(__clang__)

} // http
} // beast

#endif
