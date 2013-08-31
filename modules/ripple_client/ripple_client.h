//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Client classes.

    This module provides classes that perform client interaction with the server.

    @defgroup ripple_client
*/

#ifndef RIPPLE_CLIENT_H
#define RIPPLE_CLIENT_H

#include "beast/modules/beast_core/beast_core.h" // Must come before <boost/bind.hpp>

#include <boost/unordered_set.hpp> // InfoSub

#include "../ripple_core/ripple_core.h"
#include "../ripple_data/ripple_data.h"

namespace ripple
{

#include "../ripple_app/rpc/RPCUtil.h" // only for RPCServerHandler
#include "../ripple_app/rpc/CallRPC.h"
 #include "../ripple_app/misc/ripple_InfoSub.h"
#include "../ripple_app/rpc/RPCSub.h"

}

#endif
