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

#ifndef BEAST_HTTP_BASIC_MESSAGE_H_INCLUDED
#define BEAST_HTTP_BASIC_MESSAGE_H_INCLUDED

#include <memory>

namespace beast {
namespace http {

namespace method {
enum methodc_t
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
    http_purge
};
} // method

class basic_message
{
private:

public:
};

class basic_request
{
public:

};

}
}

#endif
