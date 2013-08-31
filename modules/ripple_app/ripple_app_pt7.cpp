//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

namespace ripple
{

#include "ledger/ripple_InboundLedgers.cpp"
#include "ledger/ripple_LedgerHistory.cpp"
#include "misc/ripple_SerializedLedger.cpp"
#include "tx/ripple_TransactionAcquire.cpp"

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4018) // signed/unsigned mismatch
#endif
#include "misc/NetworkOPs.cpp"
#include "peers/ripple_Peers.cpp"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

}
