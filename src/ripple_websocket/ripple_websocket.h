//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WEBSOCKET_H_INCLUDED
#define RIPPLE_WEBSOCKET_H_INCLUDED

// needed before inclusion of stdint.h for INT32_MIN/INT32_MAX macros
#ifndef __STDC_LIMIT_MACROS
  #define __STDC_LIMIT_MACROS
#endif

#include "../ripple_basics/ripple_basics.h"

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "websocket/src/common.hpp"
#include "websocket/src/sockets/socket_base.hpp"
#include "autosocket/AutoSocket.h" // must come before autotls.hpp
#include "websocket/src/sockets/autotls.hpp"
#include "websocket/src/websocketpp.hpp"
#include "websocket/src/logger/logger.hpp"

#endif
