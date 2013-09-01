//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

#include "../ripple_client/ripple_client.h"
#include "../ripple_net/ripple_net.h"
#include "../ripple_websocket/ripple_websocket.h"

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4309) // truncation of constant value
#endif

namespace ripple
{

# include "rpc/RPCErr.h"
#include "paths/ripple_PathRequest.cpp" // needs RPCErr.h
#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"

#include "main/ParameterTable.cpp"
#include "peers/PeerDoor.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"

}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
