//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_core.h"

#include <fstream>

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_set.hpp>

// For NodeStore backends
#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_mdb/ripple_mdb.h"

namespace ripple
{

#include "functional/ripple_Config.cpp"
# include "functional/ripple_LoadFeeTrack.h" // private
#include "functional/ripple_LoadFeeTrack.cpp"
#include "functional/ripple_Job.cpp"
#include "functional/ripple_JobQueue.cpp"
#include "functional/ripple_LoadEvent.cpp"
#include "functional/ripple_LoadMonitor.cpp"

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
#include "node/NodeStore.cpp"
#include "node/NodeObject.cpp"

#  include "test/Results.h"
#  include "test/SimplePayload.h"
#  include "test/MessageType.h"
#  include "test/ConnectionType.h"
#  include "test/PeerType.h"
#  include "test/NetworkType.h"
#  include "test/StateBase.h"
#  include "test/PeerLogicBase.h"
#  include "test/InitPolicy.h"
# include "test/ConfigType.h"
#include "test/TestOverlay.cpp"

# include "validator/Validator.h"
#include "validator/Validator.cpp"
# include "validator/ValidatorSourceStrings.h"
# include "validator/ValidatorSourceTrustedUri.h"
# include "validator/ValidatorsImp.h" // private
#include "validator/ValidatorSourceStrings.cpp"
#include "validator/ValidatorSourceTrustedUri.cpp"
#include "validator/Validators.cpp"

}
