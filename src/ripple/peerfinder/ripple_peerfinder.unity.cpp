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

#include "../../BeastConfig.h"

#include "ripple_peerfinder.h"

#include "../../ripple/algorithm/api/CycledSet.h"
#include "../../ripple/common/Resolver.h"

#include <deque>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <unordered_set>

#include "../../beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/array.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>

#include "../../beast/modules/beast_sqdb/beast_sqdb.h"
#include "../../beast/modules/beast_asio/beast_asio.h"

#include "../../beast/beast/boost/ErrorCode.h"
#include "../../beast/beast/chrono/chrono_io.h"

#include "impl/iosformat.h" // VFALCO NOTE move to beast

#ifndef NDEBUG
# define consistency_check(cond) bassert(cond)
#else
# define consistency_check(cond)
#endif

#include "impl/PrivateTypes.h"
#include "impl/Tuning.h"
#include "impl/Checker.h"
#include "impl/CheckerAdapter.h"
#include "impl/Livecache.h"
#include "impl/SlotImp.h"
#include "impl/Counts.h"
#include "impl/Source.h"
#include "impl/SourceStrings.h"
#include "impl/Store.h"
#include "impl/Bootcache.h"
#include "impl/StoreSqdb.h"
#include "impl/Reporting.h"
#include "impl/Logic.h"

#include "impl/Bootcache.cpp"
#include "impl/Checker.cpp"
#include "impl/Config.cpp"
#include "impl/ConnectHandouts.cpp"
#include "impl/Endpoint.cpp"
#include "impl/Livecache.cpp"
#include "impl/Manager.cpp"
#include "impl/RedirectHandouts.cpp"
#include "impl/SlotHandouts.cpp"
#include "impl/SlotImp.cpp"
#include "impl/SourceStrings.cpp"

#include "sim/GraphAlgorithms.h"
#include "sim/WrappedSink.h"
#include "sim/Predicates.h"
#include "sim/FunctionQueue.h"
#include "sim/Message.h"
#include "sim/NodeSnapshot.h"
#include "sim/Params.h"
#include "sim/Tests.cpp"
