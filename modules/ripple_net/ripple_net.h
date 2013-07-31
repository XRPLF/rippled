//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_H_INCLUDED
#define RIPPLE_NET_H_INCLUDED

/** Include this to get the @ref ripple_net module.

    @file ripple_net.h
    @ingroup ripple_net
*/

/** Network classes.

    This module provides classes that handle all network activities.

    @defgroup ripple_net
*/

#include "../ripple_basics/ripple_basics.h"

#include "../ripple_websocket/ripple_websocket.h"

namespace ripple
{

#include "basics/ripple_HTTPRequest.h"
#include "basics/ripple_HttpsClient.h"
#include "basics/ripple_RPCServer.h"
#include "basics/ripple_SNTPClient.h"

#include "protocol/ripple_ProxyProtocol.h"

}

#endif
