//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WEBSOCKET_RIPPLEHEADER
#define RIPPLE_WEBSOCKET_RIPPLEHEADER

/** Include this to get the @ref ripple_websocket module.

    This module provides support for websockets. It requires both
    boost::asio and OpenSSL. It is a fork of the original websocketpp project
    written in C++.

    @note This code contains bugs including one known deadlock. It's a terrible
          mess and it needs to be replaced.

    @file ripple_websocket.h
    @ingroup ripple_websocket
    @deprecated
*/


// VFALCO NOTE Log dependencies have wormed their way into websocketpp,
//             which needs the ripple_basic module to compile.
//
//        TODO Remove the dependency on ripple_basics and Log.
//             Perhaps by using an adapter.
//
#include "../modules/ripple_basics/ripple_basics.h"

#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>

//------------------------------------------------------------------------------

// VFALCO TODO This include is just to prevent a warning about
//             redefinition of __STDC_LIMIT_MACROS. Fix it right.
//
#include "websocket/src/rng/boost_rng.hpp"

#include "websocket/src/common.hpp"
#include "websocket/src/sockets/socket_base.hpp"

#include "autosocket/ripple_AutoSocket.h" // must come before autotls.hpp

#include "websocket/src/sockets/autotls.hpp"
#include "websocket/src/websocketpp.hpp"
#include "websocket/src/logger/logger.hpp"

#endif
