//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_frame.h"

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/unordered_map.hpp>

#include "api/HTTPServer.cpp"
#include "api/RPCService.cpp"
