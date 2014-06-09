//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include <ripple/unity/websocket.h>

// Unity build file for websocket
//

#include <websocket/src/sha1/sha1.h>

// Must come first to prevent compile errors
#include <websocket/src/uri.cpp>

#include <websocket/src/base64/base64.cpp>
#include <websocket/src/messages/data.cpp>
#include <websocket/src/processors/hybi_header.cpp>
#include <websocket/src/processors/hybi_util.cpp>
#include <websocket/src/md5/md5.c>
#include <websocket/src/network_utilities.cpp>
#include <websocket/src/sha1/sha1.cpp>

#include <ripple/module/websocket/autosocket/AutoSocket.cpp>
#include <ripple/module/websocket/autosocket/LogWebsockets.cpp>
