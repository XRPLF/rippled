//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include "ripple_websocket.h"

// Unity build file for websocket
//

#include "websocket/src/sha1/sha1.h"

// Must come first to prevent compile errors
#include "websocket/src/uri.cpp"

#include "websocket/src/base64/base64.cpp"
#include "websocket/src/rng/boost_rng.cpp"
#include "websocket/src/messages/data.cpp"
#include "websocket/src/processors/hybi_header.cpp"
#include "websocket/src/processors/hybi_util.cpp"
#include "websocket/src/md5/md5.c"
#include "websocket/src/network_utilities.cpp"
#include "websocket/src/sha1/sha1.cpp"

#include "autosocket/AutoSocket.cpp"
#include "autosocket/LogWebsockets.cpp"
