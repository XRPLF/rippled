//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/optional.hpp>

#include "ripple_app.h"

#include "../ripple_net/ripple_net.h"
#include "../ripple_websocket/ripple_websocket.h"

// This .cpp will end up including all of the public header
// material in Ripple since it holds the Application object.

#include "../ripple/http/ripple_http.h"
#include "../ripple/rpc/ripple_rpc.h"
#include "../ripple/validators/ripple_validators.h"

namespace ripple
{

//
// Application
//

# include "main/NodeStoreScheduler.h"
#include "main/NodeStoreScheduler.cpp"

# include "main/IoServicePool.h"
#include "main/IoServicePool.cpp"

# include "main/FatalErrorReporter.h"
#include "main/FatalErrorReporter.cpp"

# include "peers/PeerDoor.h"
#include "peers/PeerDoor.cpp"

# include "rpc/RPCHandler.h"
#   include "misc/PowResult.h"
#  include "misc/ProofOfWork.h"
# include "misc/ProofOfWorkFactory.h"
#include "rpc/RPCHandler.cpp"

# include "rpc/RPCServerHandler.h"
# include "main/RPCHTTPServer.h"
#include "main/RPCHTTPServer.cpp"
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

#include "main/Application.cpp"

//
// RippleMain
//
# include "main/RippleMain.h"
# include "node/SqliteFactory.h"
#include "node/SqliteFactory.cpp"
#include "main/RippleMain.cpp"

}

//------------------------------------------------------------------------------

struct ProtobufLibrary
{
    ~ProtobufLibrary ()
    {
        google::protobuf::ShutdownProtobufLibrary();
    }
};

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
    //
    // These debug heap calls do nothing in release or non Visual Studio builds.
    //

    // Checks the heap at every allocation and deallocation (slow).
    //
    //beast::Debug::setAlwaysCheckHeap (false);

    // Keeps freed memory blocks and fills them with a guard value.
    //
    //beast::Debug::setHeapDelayedFree (false);

    // At exit, reports all memory blocks which have not been freed.
    //
#if RIPPLE_DUMP_LEAKS_ON_EXIT
    beast::Debug::setHeapReportLeaks (true);
#else
    beast::Debug::setHeapReportLeaks (false);
#endif

    beast::SharedSingleton <ProtobufLibrary>::get ();

    int result (0);

    ripple::RippleMain rippled;

    result = rippled.runFromMain (argc, argv);

    return result;
}

