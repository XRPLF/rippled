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

#include <BeastConfig.h>

#include <ripple/unity/app.h>

#include <ripple/common/seconds_clock.h>

#include <ripple/app/shamap/SHAMap.cpp> // Uses theApp
#include <ripple/app/shamap/SHAMapItem.cpp>
#include <ripple/app/shamap/SHAMapSync.cpp>
#include <ripple/app/shamap/SHAMapMissingNode.cpp>

#include <ripple/app/misc/CanonicalTXSet.cpp>
#include <ripple/app/ledger/LedgerProposal.cpp>
#include <ripple/app/main/LoadManager.cpp>
#include <ripple/app/ledger/BookListeners.cpp>
#include <ripple/app/ledger/OrderBookDB.cpp>

#include <ripple/app/data/Database.cpp>
#include <ripple/app/data/DatabaseCon.cpp>
#include <ripple/app/data/SqliteDatabase.cpp>
#include <ripple/app/data/DBInit.cpp>

#include <ripple/app/shamap/RadixMapTest.h>
#include <ripple/app/shamap/RadixMapTest.cpp>
#include <ripple/app/shamap/FetchPackTests.cpp>
