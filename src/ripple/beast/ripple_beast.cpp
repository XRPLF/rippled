//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

#include "../beast/beast/asio/Asio.cpp"
#include "../beast/beast/boost/Boost.cpp"
#include "../beast/beast/chrono/Chrono.cpp"
#include "../beast/beast/crypto/Crypto.cpp"
#include "../beast/beast/http/HTTP.cpp"
#include "../beast/beast/insight/Insight.cpp"
#include "../beast/beast/net/Net.cpp"
#include "../beast/beast/smart_ptr/SmartPtr.cpp"
#include "../beast/beast/strings/Strings.cpp"
#include "../beast/beast/threads/Threads.cpp"
#include "../beast/beast/utility/Utility.cpp"
