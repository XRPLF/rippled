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

#include <ripple/unity/peerfinder.h>

#include <ripple/algorithm/api/CycledSet.h>
#include <ripple/common/Resolver.h>

#include <deque>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <unordered_set>

#include <boost/array.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>

#include <beast/module/sqdb/sqdb.h>
#include <beast/module/asio/asio.h>

#include <beast/boost/ErrorCode.h>
#include <beast/chrono/chrono_io.h>

#include <ripple/peerfinder/impl/iosformat.h> // VFALCO NOTE move to beast

#ifndef NDEBUG
# define consistency_check(cond) bassert(cond)
#else
# define consistency_check(cond)
#endif

#include <ripple/peerfinder/impl/PrivateTypes.h>
#include <ripple/peerfinder/impl/Tuning.h>
#include <ripple/peerfinder/impl/Checker.h>
#include <ripple/peerfinder/impl/CheckerAdapter.h>
#include <ripple/peerfinder/impl/Livecache.h>
#include <ripple/peerfinder/impl/SlotImp.h>
#include <ripple/peerfinder/impl/Counts.h>
#include <ripple/peerfinder/impl/Source.h>
#include <ripple/peerfinder/impl/SourceStrings.h>
#include <ripple/peerfinder/impl/Store.h>
#include <ripple/peerfinder/impl/Bootcache.h>
#include <ripple/peerfinder/impl/StoreSqdb.h>
#include <ripple/peerfinder/impl/Reporting.h>
#include <ripple/peerfinder/impl/Logic.h>

#include <ripple/peerfinder/impl/Bootcache.cpp>
#include <ripple/peerfinder/impl/Checker.cpp>
#include <ripple/peerfinder/impl/Config.cpp>
#include <ripple/peerfinder/impl/ConnectHandouts.cpp>
#include <ripple/peerfinder/impl/Endpoint.cpp>
#include <ripple/peerfinder/impl/Livecache.cpp>
#include <ripple/peerfinder/impl/Manager.cpp>
#include <ripple/peerfinder/impl/RedirectHandouts.cpp>
#include <ripple/peerfinder/impl/SlotHandouts.cpp>
#include <ripple/peerfinder/impl/SlotImp.cpp>
#include <ripple/peerfinder/impl/SourceStrings.cpp>

#include <ripple/peerfinder/sim/GraphAlgorithms.h>
#include <ripple/peerfinder/sim/WrappedSink.h>
#include <ripple/peerfinder/sim/Predicates.h>
#include <ripple/peerfinder/sim/FunctionQueue.h>
#include <ripple/peerfinder/sim/Message.h>
#include <ripple/peerfinder/sim/NodeSnapshot.h>
#include <ripple/peerfinder/sim/Params.h>
#include <ripple/peerfinder/sim/Tests.cpp>
