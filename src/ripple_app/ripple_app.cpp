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

#include "../../BeastConfig.h"

#include "../beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/optional.hpp>
#include <boost/version.hpp>

#include "ripple_app.h"

#include "../ripple_net/ripple_net.h"
#include "../ripple_rpc/ripple_rpc.h"
#include "../ripple_websocket/ripple_websocket.h"

// This .cpp will end up including all of the public header
// material in Ripple since it holds the Application object.

#include "../ripple/common/seconds_clock.h"
#include "../ripple/http/ripple_http.h"
#include "../ripple/resource/ripple_resource.h"
#include "../ripple/sitefiles/ripple_sitefiles.h"
#include "../ripple/validators/ripple_validators.h"

#include "../beast/beast/asio/io_latency_probe.h"
#include "../beast/beast/cxx14/memory.h"

# include "main/CollectorManager.h"
#include "main/CollectorManager.cpp"

#include "misc/ProofOfWorkFactory.h"

# include "main/NodeStoreScheduler.h"
#include "main/NodeStoreScheduler.cpp"

# include "main/IoServicePool.h"
#include "main/IoServicePool.cpp"

# include "main/FatalErrorReporter.h"
#include "main/FatalErrorReporter.cpp"

# include "rpc/RPCHandler.h"
# include "rpc/RPCServerHandler.h"
# include "main/RPCHTTPServer.h"
#include "main/RPCHTTPServer.cpp"
#include "rpc/RPCServerHandler.cpp"
#include "rpc/RPCHandler.cpp"

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

#include "../ripple/common/ResolverAsio.h"

# include "node/SqliteFactory.h"
#include "node/SqliteFactory.cpp"

#include "main/Application.cpp"

#include "main/Main.cpp"

//------------------------------------------------------------------------------

namespace ripple {
int run (int argc, char** argv);
}

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
#if defined(__GNUC__) && !defined(__clang__)
    auto constexpr gccver = (__GNUC__ * 100 * 100) +
                            (__GNUC_MINOR__ * 100) +
                            __GNUC_PATCHLEVEL__;

    static_assert (gccver >= 40801,
        "GCC version 4.8.1 or later is required to compile rippled.");
#endif

#ifdef _MSC_VER
    static_assert (_MSC_VER >= 1800,
        "Visual Studio 2013 or later is required to compile rippled.");
#endif

    static_assert (BOOST_VERSION >= 105500,
        "Boost version 1.55 or later is required to compile rippled");
        
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

    return ripple::run (argc, argv);
}
