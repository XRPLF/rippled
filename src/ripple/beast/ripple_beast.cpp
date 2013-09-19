//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*  This file includes all of the beast sources needed to link.
    By including them here, we avoid having to muck with the SConstruct
    Makefile, Project file, or whatever.
*/

// MUST come first!
#include "BeastConfig.h"

// Include this to get all the basic includes included, to prevent errors
#include "../beast/modules/beast_core/beast_core.h"

// Mac builds use ripple_beastobjc.mm
#ifndef BEAST_MAC
# include "../beast/modules/beast_core/beast_core.cpp"
#endif

#include "../beast/modules/beast_asio/beast_asio.cpp"
#include "../beast/modules/beast_crypto/beast_crypto.cpp"
#include "../beast/modules/beast_db/beast_db.cpp"
#include "../beast/modules/beast_sqdb/beast_sqdb.cpp"

#include "../beast/beast/net/Net.cpp"
