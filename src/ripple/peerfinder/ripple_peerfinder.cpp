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

#include "BeastConfig.h"

#include "ripple_peerfinder.h"

#include "../../ripple/types/api/AgedHistory.h"

#include <set>

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>

#include "beast/modules/beast_sqdb/beast_sqdb.h"
#include "beast/modules/beast_asio/beast_asio.h"

#include "beast/beast/boost/ErrorCode.h"

namespace ripple {
using namespace beast;
}

#  include "impl/Tuning.h"
# include "impl/Checker.h"
#include "impl/CheckerAdapter.h"
#  include "impl/CachedEndpoint.h"
#include "impl/EndpointCache.h"
#include "impl/Slots.h"
#include "impl/Source.h"
#include "impl/SourceStrings.h"
#  include "impl/LegacyEndpoint.h"
#  include "impl/Store.h"
# include "impl/LegacyEndpointCache.h"
# include "impl/PeerInfo.h"
#include "impl/StoreSqdb.h"
#include "impl/Logic.h"

#include "impl/Checker.cpp"
#include "impl/Config.cpp"
#include "impl/Endpoint.cpp"
#include "impl/EndpointCache.cpp"
#include "impl/Manager.cpp"
#include "impl/Slots.cpp"
#include "impl/SourceStrings.cpp"
#include "impl/Tests.cpp"
