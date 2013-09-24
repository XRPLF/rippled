//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_validators.h"

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/regex.hpp>
#include <boost/unordered_set.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>

#include <set>

#include "beast/modules/beast_asio/beast_asio.h"
#include "beast/modules/beast_sqdb/beast_sqdb.h"

#include "../ripple_data/ripple_data.h" // for RippleAddress REMOVE ASAP

#include "../testoverlay/ripple_testoverlay.h" // for unit test

namespace ripple {
using namespace beast;
}

# include "impl/CancelCallbacks.h"
# include "impl/ChosenList.h"
# include "impl/SourceFile.h"
# include "impl/SourceStrings.h"
# include "impl/SourceURL.h"
# include "impl/Utilities.h"
#  include "impl/SourceDesc.h"
# include "impl/Store.h"
# include "impl/StoreSqdb.h"
#include "impl/Logic.h"

#include "impl/Manager.cpp"
#include "impl/Source.cpp"
#include "impl/SourceFile.cpp"
#include "impl/SourceStrings.cpp"
#include "impl/SourceURL.cpp"
#include "impl/StoreSqdb.cpp"
#include "impl/Tests.cpp"
#include "impl/Utilities.cpp"
