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

#include <ripple/unity/validators.h>

#include <boost/regex.hpp>
#include <boost/unordered_set.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>

#include <set>

#include <beast/threads/ScopedWrapperContext.h>
#include <beast/module/asio/asio.h>
#include <beast/module/sqdb/sqdb.h>

#include <ripple/algorithm/api/CycledSet.h>
#include <ripple/unity/testoverlay.h> // for unit test

#include <ripple/validators/impl/Tuning.h>
#include <ripple/validators/impl/ChosenList.h>
#include <ripple/validators/impl/Count.h>
#include <ripple/validators/impl/SourceFile.h>
#include <ripple/validators/impl/SourceStrings.h>
#include <ripple/validators/impl/SourceURL.h>
#include <ripple/validators/impl/SourceDesc.h>
#include <ripple/validators/impl/Store.h>
#include <ripple/validators/impl/StoreSqdb.h>
#include <ripple/validators/impl/Utilities.h>
#include <ripple/validators/impl/Validation.h>
#include <ripple/validators/impl/Validator.h>
#include <ripple/validators/impl/Logic.h>

#include <ripple/validators/impl/Manager.cpp>
#include <ripple/validators/impl/Source.cpp>
#include <ripple/validators/impl/SourceFile.cpp>
#include <ripple/validators/impl/SourceStrings.cpp>
#include <ripple/validators/impl/SourceURL.cpp>
#include <ripple/validators/impl/StoreSqdb.cpp>
#include <ripple/validators/impl/Tests.cpp>
#include <ripple/validators/impl/Utilities.cpp>
