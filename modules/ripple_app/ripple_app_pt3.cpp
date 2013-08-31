//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

namespace ripple
{

#include "rpc/RPCErr.h" // private
#include "rpc/RPCUtil.h" // private
#include "websocket/WSConnection.h" // private

#include "rpc/RPCErr.cpp"
#include "rpc/RPCUtil.cpp"

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#endif
#include "rpc/CallRPC.cpp"
#include "rpc/RPCHandler.cpp"
#ifdef _MSC_VER
#pragma warning (pop)
#endif

#include "rpc/RPCSub.cpp"

#include "basics/ripple_RPCServerHandler.cpp" // needs RPCUtil
#include "paths/ripple_PathRequest.cpp" // needs RPCErr.h
#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"

#include "main/ParameterTable.cpp"
#include "peers/PeerDoor.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4309) // truncation of constant value
#endif
#include "websocket/WSConnection.cpp"
#include "websocket/WSDoor.cpp"
#include "websocket/WSServerHandler.cpp"
#ifdef _MSC_VER
#pragma warning (pop)
#endif

}

