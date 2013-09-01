//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "beast/modules/beast_core/beast_core.h" // Must come before <boost/bind.hpp>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include "ripple_app.h"

namespace ripple
{

#include "peers/ripple_PeerSet.cpp"
#include "misc/ripple_OrderBook.cpp"
#   include "misc/PowResult.h"
#  include "misc/ProofOfWork.h"
# include "misc/ProofOfWorkFactory.h"
#include "misc/ProofOfWorkFactory.cpp"
#include "misc/ProofOfWork.cpp"
#include "misc/ripple_SerializedTransaction.cpp"

#include "shamap/ripple_SHAMapSyncFilters.cpp" // requires Application

#include "consensus/ripple_LedgerConsensus.cpp"

#include "ledger/LedgerMaster.cpp"

}
