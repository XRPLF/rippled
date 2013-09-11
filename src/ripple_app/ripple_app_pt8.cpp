//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4309) // truncation of constant value
#endif

namespace ripple
{

#include "paths/ripple_PathRequest.cpp"
#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"

#include "main/ParameterTable.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"

}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
