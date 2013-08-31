//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"

namespace ripple
{

#include "peers/ripple_PeerSet.cpp"
#include "misc/ripple_InfoSub.cpp"
#include "misc/ripple_OrderBook.cpp"
#include "misc/ripple_ProofOfWork.cpp"
#include "misc/ripple_ProofOfWorkFactory.h"
#include "misc/ripple_ProofOfWorkFactory.cpp" // requires ProofOfWork.cpp for ProofOfWork::sMaxDifficulty
#include "misc/ripple_SerializedTransaction.cpp"

#include "shamap/ripple_SHAMapSyncFilters.cpp" // requires Application

 #include "main/ripple_FatalErrorReporter.h"
#include "main/ripple_FatalErrorReporter.cpp"
 #include "main/ripple_RippleMain.h"
 #include "node/ripple_HyperLevelDBBackendFactory.h"
 #include "node/ripple_KeyvaDBBackendFactory.h"
 #include "node/ripple_LevelDBBackendFactory.h"
 #include "node/ripple_MdbBackendFactory.h"
 #include "node/ripple_MemoryBackendFactory.h"
 #include "node/ripple_NullBackendFactory.h"
 #include "node/ripple_SqliteBackendFactory.h"
#include "main/ripple_RippleMain.cpp"

#include "node/ripple_HyperLevelDBBackendFactory.cpp"
#include "node/ripple_KeyvaDBBackendFactory.cpp"
#include "node/ripple_LevelDBBackendFactory.cpp"
#include "node/ripple_MemoryBackendFactory.cpp"
#include "node/ripple_NullBackendFactory.cpp"
#include "node/ripple_MdbBackendFactory.cpp"
#include "node/ripple_SqliteBackendFactory.cpp"

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4018) // signed/unsigned mismatch
#endif
#include "consensus/ripple_LedgerConsensus.cpp"
#include "ledger/LedgerMaster.cpp"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

}

//------------------------------------------------------------------------------

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
    ripple::RippleMain rippled;
    return rippled.runFromMain (argc, argv);
}
