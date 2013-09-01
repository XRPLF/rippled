//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

#include "beast/modules/beast_db/beast_db.h"

#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_net/ripple_net.h"
#include "../ripple_websocket/ripple_websocket.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_mdb/ripple_mdb.h"

// This .cpp will end up including all of the public header
// material in Ripple since it holds the Application object.

namespace ripple
{

//
// Application
//

# include "boost/ripple_IoService.h" // deprecated
#include "boost/ripple_IoService.cpp" // deprecated

# include "main/ripple_FatalErrorReporter.h"
#include "main/ripple_FatalErrorReporter.cpp"

# include "rpc/RPCHandler.h"
#include "rpc/RPCHandler.cpp"
# include "rpc/RPCServerHandler.h"
#include "rpc/RPCServerHandler.cpp"
#include "websocket/WSConnection.h"
# include "websocket/WSServerHandler.h"
#include "websocket/WSServerHandler.cpp"
#include "websocket/WSConnection.cpp"
# include "websocket/WSDoor.h"
#include "websocket/WSDoor.cpp"

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4018) // signed/unsigned mismatch
#endif
#include "main/ripple_Application.cpp"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

//
// RippleMain
//

#  include "node/ripple_HyperLevelDBBackendFactory.h"
# include "node/ripple_HyperLevelDBBackendFactory.cpp"
#  include "node/ripple_KeyvaDBBackendFactory.h"
# include "node/ripple_KeyvaDBBackendFactory.cpp"
#  include "node/ripple_LevelDBBackendFactory.h"
# include "node/ripple_LevelDBBackendFactory.cpp"
#  include "node/ripple_MemoryBackendFactory.h"
# include "node/ripple_MemoryBackendFactory.cpp"
#  include "node/ripple_NullBackendFactory.h"
# include "node/ripple_NullBackendFactory.cpp"
#  include "node/ripple_MdbBackendFactory.h"
# include "node/ripple_MdbBackendFactory.cpp"
#  include "node/ripple_SqliteBackendFactory.h"
# include "node/ripple_SqliteBackendFactory.cpp"
# include "main/ripple_RippleMain.h"
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
