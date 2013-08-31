//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

#include "../ripple_client/ripple_client.h"
#include "../ripple_net/ripple_net.h"

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4309) // truncation of constant value
#endif

namespace ripple
{

 #include "rpc/RPCErr.h"
 #include "rpc/RPCUtil.h"
#include "websocket/WSConnection.h" // needs RPCErr

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#endif
#include "rpc/RPCHandler.cpp"
#ifdef _MSC_VER
#pragma warning (pop)
#endif

#include "paths/ripple_PathRequest.cpp" // needs RPCErr.h
#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"

#include "main/ParameterTable.cpp"
#include "peers/PeerDoor.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"

#include "websocket/WSConnection.cpp"
#include "websocket/WSDoor.cpp"
#include "websocket/WSServerHandler.cpp"

}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
