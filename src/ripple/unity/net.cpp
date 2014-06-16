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

/** Add this to get the @ref ripple_net module.

    @file ripple_net.cpp
    @ingroup ripple_net
*/

#undef DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER /**/

#include <BeastConfig.h>

#include <boost/version.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/make_shared.hpp>
#include <boost/array.hpp>
#include <boost/foreach.hpp>
#include <boost/mpl/at.hpp>
#include <boost/mpl/vector.hpp>

#include <ripple/unity/net.h>

#include <ripple/unity/websocket.h> // for HTTPClient, RPCDoor

// VFALCO NOTE This is the "new new new" where individual headers are included
//             directly (instead of the module header). The corresponding .cpp
//             still uses the unity style inclusion.
//
#include <ripple/module/rpc/ErrorCodes.h>
#include <ripple/common/jsonrpc_fields.h>

#include <ripple/module/net/basics/HTTPRequest.cpp>
#include <ripple/module/net/basics/HTTPClient.cpp>
#include <ripple/module/net/basics/impl/RPCServerImp.h>
#include <ripple/module/net/basics/SNTPClient.cpp>
#include <ripple/module/net/rpc/RPCCall.cpp>
#include <ripple/module/net/rpc/RPCErr.cpp>
#include <ripple/module/net/rpc/RPCSub.cpp>
#include <ripple/module/net/rpc/RPCUtil.cpp>
#include <ripple/module/net/rpc/InfoSub.cpp>

#include <ripple/module/net/basics/RPCDoor.cpp>
