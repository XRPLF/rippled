//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "beast/modules/beast_core/beast_core.h"

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/unordered_map.hpp>

#include "ripple_frame.h"

#include "api/RPCService.cpp"
#include "api/Stoppable.cpp"
