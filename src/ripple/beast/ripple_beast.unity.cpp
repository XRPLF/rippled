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
#include "../../BeastConfig.h"

// Include this to get all the basic includes included, to prevent errors
#include "../../beast/modules/beast_core/beast_core.unity.cpp"
#include "../../beast/modules/beast_asio/beast_asio.unity.cpp"
#include "../../beast/modules/beast_sqdb/beast_sqdb.unity.cpp"

#include "../../beast/beast/asio/Asio.unity.cpp"
#include "../../beast/beast/boost/Boost.unity.cpp"
#include "../../beast/beast/chrono/Chrono.unity.cpp"
#include "../../beast/beast/container/Container.unity.cpp"
#include "../../beast/beast/crypto/Crypto.unity.cpp"
#include "../../beast/beast/http/HTTP.unity.cpp"
#include "../../beast/beast/insight/Insight.unity.cpp"
#include "../../beast/beast/net/Net.unity.cpp"
#include "../../beast/beast/streams/streams.unity.cpp"
#include "../../beast/beast/strings/Strings.unity.cpp"
#include "../../beast/beast/threads/Threads.unity.cpp"
#include "../../beast/beast/utility/Utility.unity.cpp"

#include "../../beast/beast/cxx14/cxx14.unity.cpp"

#include "../../beast/beast/unit_test/define_print.cpp"
