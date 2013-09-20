//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// backend support
#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple/sophia/ripple_sophia.h"

namespace ripple
{

#  include "impl/DecodedBlob.h"
#  include "impl/EncodedBlob.h"
#  include "impl/BatchWriter.h"
# include "backend/HyperDBFactory.h"
#include "backend/HyperDBFactory.cpp"
# include "backend/KeyvaDBFactory.h"
#include "backend/KeyvaDBFactory.cpp"
# include "backend/LevelDBFactory.h"
#include "backend/LevelDBFactory.cpp"
# include "backend/MemoryFactory.h"
#include "backend/MemoryFactory.cpp"
# include "backend/NullFactory.h"
#include "backend/NullFactory.cpp"
# include "backend/MdbFactory.h"
#include "backend/MdbFactory.cpp"
# include "backend/SophiaFactory.h"
#include "backend/SophiaFactory.cpp"

#include "impl/BatchWriter.cpp"
# include "impl/Factories.h"
# include "impl/DatabaseImp.h"
#include "impl/DummyScheduler.cpp"
#include "impl/DecodedBlob.cpp"
#include "impl/EncodedBlob.cpp"
#include "impl/NodeObject.cpp"

# include "tests/TestBase.h"
#include "tests/BackendTests.cpp"
#include "tests/BasicTests.cpp"
#include "tests/DatabaseTests.cpp"
#include "tests/TimingTests.cpp"

}
