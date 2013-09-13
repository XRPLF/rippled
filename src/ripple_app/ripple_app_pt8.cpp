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

#include "paths/PathRequest.cpp"
#include "paths/RippleCalc.cpp"
#include "paths/PathState.cpp"

#include "main/ParameterTable.cpp"
#include "paths/RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"

}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
