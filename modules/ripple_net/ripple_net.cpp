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

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/version.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp> // for unit test
#include <boost/mpl/at.hpp>
#include <boost/mpl/vector.hpp>

#include "ripple_net.h"

#include "../ripple_websocket/ripple_websocket.h" // for HTTPClient, RPCDoor

namespace ripple
{

#include "basics/impl/MultiSocketType.h"
#include "basics/RippleSSLContext.cpp"
#include "basics/MultiSocket.cpp"
#include "basics/HTTPRequest.cpp"
#include "basics/HTTPClient.cpp"
# include "basics/impl/RPCServerImp.h"
#include "basics/RPCDoor.cpp"
#include "basics/SNTPClient.cpp"

#include "rpc/RPCCall.cpp"
#include "rpc/RPCErr.cpp"
#include "rpc/RPCSub.cpp"
#include "rpc/RPCUtil.cpp"
#include "rpc/InfoSub.cpp"

}
