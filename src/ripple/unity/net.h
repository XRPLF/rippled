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

#ifndef RIPPLE_NET_H_INCLUDED
#define RIPPLE_NET_H_INCLUDED

#include <boost/unordered_set.hpp> // For InfoSub

#include <boost/asio.hpp>
#undef DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER /**/
#include <boost/asio/ssl.hpp>

#include <ripple/unity/resource.h>

#include <ripple/unity/basics.h>
#include <ripple/unity/core.h>
#include <ripple/unity/data.h>
#include <ripple/module/websocket/autosocket/AutoSocket.h>

#include <ripple/module/net/basics/HTTPRequest.h>
#include <ripple/module/net/basics/HTTPClient.h>
#include <ripple/module/net/basics/RPCServer.h>
#include <ripple/module/net/basics/RPCDoor.h>
#include <ripple/module/net/basics/SNTPClient.h>

#include <ripple/module/net/rpc/RPCErr.h>
#include <ripple/module/net/rpc/RPCUtil.h>
#include <ripple/module/net/rpc/RPCCall.h>
#include <ripple/module/net/rpc/InfoSub.h>
#include <ripple/module/net/rpc/RPCSub.h>

#endif
