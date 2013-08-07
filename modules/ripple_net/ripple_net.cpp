//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_net module.

    @file ripple_net.cpp
    @ingroup ripple_net
*/

#include "BeastConfig.h"

#include "ripple_net.h"

// VFALCO TODO Remove this dependency on theConfig
#include "../modules/ripple_core/ripple_core.h" // theConfig for HttpsClient

#include "beast/modules/beast_asio/beast_asio.h"

namespace ripple
{

#include "basics/ripple_HTTPRequest.cpp"
#include "basics/ripple_HttpsClient.cpp"
#include "basics/ripple_RPCServer.cpp"
#include "basics/ripple_SNTPClient.cpp"

}
