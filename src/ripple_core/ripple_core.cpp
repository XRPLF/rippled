//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_core.h"

// Needed for InputParser
#include "beast/modules/beast_asio/beast_asio.h"

#include <fstream>

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

// For NodeStore backends
#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple/sophia/ripple_sophia.h"

namespace ripple
{

#include "functional/Config.cpp"
# include "functional/LoadFeeTrack.h" // private
#include "functional/LoadFeeTrack.cpp"
#include "functional/Job.cpp"
#include "functional/JobQueue.cpp"
#include "functional/LoadEvent.cpp"
#include "functional/LoadMonitor.cpp"

#  include "node/HyperLevelDBBackendFactory.h"
# include "node/HyperLevelDBBackendFactory.cpp"
#  include "node/KeyvaDBBackendFactory.h"
# include "node/KeyvaDBBackendFactory.cpp"
#  include "node/LevelDBBackendFactory.h"
# include "node/LevelDBBackendFactory.cpp"
#  include "node/MemoryBackendFactory.h"
# include "node/MemoryBackendFactory.cpp"
#  include "node/NullBackendFactory.h"
# include "node/NullBackendFactory.cpp"
#  include "node/MdbBackendFactory.h"
# include "node/MdbBackendFactory.cpp"
#  include "node/SophiaBackendFactory.h"
# include "node/SophiaBackendFactory.cpp"
#include "node/NodeStore.cpp"
#include "node/NodeObject.cpp"

#include "peerfinder/PeerFinder.cpp"

}
