//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

namespace ripple
{

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4244) // conversion, possible loss of data
# pragma warning (disable: 4018) // signed/unsigned mismatch
#endif
#include "ledger/Ledger.cpp"
#include "node/ripple_NodeStore.cpp"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

#include "node/ripple_NodeObject.cpp"

#include "shamap/ripple_SHAMapDelta.cpp"
#include "shamap/ripple_SHAMapNode.cpp"
#include "shamap/ripple_SHAMapTreeNode.cpp"
#include "misc/ripple_AccountItems.cpp"
#include "misc/ripple_AccountState.cpp"
#include "tx/ChangeTransactor.cpp"
#include "contracts/ripple_Contract.cpp"
#include "contracts/ripple_Operation.cpp"
#include "contracts/ripple_ScriptData.cpp"
#include "contracts/ripple_Interpreter.cpp"

}
