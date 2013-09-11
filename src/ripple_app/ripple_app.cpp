//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include "ripple_app.h"

#include "../ripple_net/ripple_net.h"
#include "../ripple_websocket/ripple_websocket.h"

// This .cpp will end up including all of the public header
// material in Ripple since it holds the Application object.

namespace ripple
{

//
// Application
//

# include "main/ripple_FatalErrorReporter.h"
#include "main/ripple_FatalErrorReporter.cpp"

# include "peers/PeerDoor.h"
#include "peers/PeerDoor.cpp"

# include "rpc/RPCHandler.h"
#   include "misc/PowResult.h"
#  include "misc/ProofOfWork.h"
# include "misc/ProofOfWorkFactory.h"
#include "rpc/RPCHandler.cpp"

# include "rpc/RPCServerHandler.h"
#include "rpc/RPCServerHandler.cpp"
#include "websocket/WSConnection.h"

# include "tx/TxQueueEntry.h"
#include "tx/TxQueueEntry.cpp"
# include "tx/TxQueue.h"
#include "tx/TxQueue.cpp"

# include "websocket/WSServerHandler.h"
#include "websocket/WSServerHandler.cpp"
#include "websocket/WSConnection.cpp"
# include "websocket/WSDoor.h"
#include "websocket/WSDoor.cpp"

#include "main/ripple_Application.cpp"

//
// RippleMain
//
# include "main/ripple_RippleMain.h"
# include "node/ripple_SqliteBackendFactory.h"
#include "node/ripple_SqliteBackendFactory.cpp"
#include "main/ripple_RippleMain.cpp"

}

//------------------------------------------------------------------------------

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
    ripple::RippleMain rippled;
    return rippled.runFromMain (argc, argv);
}
