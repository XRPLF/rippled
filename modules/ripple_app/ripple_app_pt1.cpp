//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

namespace ripple
{

#include "peers/ripple_PeerSet.cpp"
#include "misc/ripple_OrderBook.cpp"
#include "misc/ripple_ProofOfWork.cpp"
#include "misc/ripple_ProofOfWorkFactory.h"
#include "misc/ripple_ProofOfWorkFactory.cpp" // requires ProofOfWork.cpp for ProofOfWork::sMaxDifficulty
#include "misc/ripple_SerializedTransaction.cpp"

#include "shamap/ripple_SHAMapSyncFilters.cpp" // requires Application

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
