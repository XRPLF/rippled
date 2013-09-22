//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_FRAME_H_INCLUDED
#define RIPPLE_FRAME_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"

// VFALCO NOTE this sucks that we have to include asio in the header
//             just for HTTPMessage!!
#include "beast/modules/beast_asio/beast_asio.h"

#include "../json/ripple_json.h"

#include "api/HTTPServer.h"
#include "api/RPCService.h"

#endif
