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

#ifndef RIPPLE_BASICS_H_INCLUDED
#define RIPPLE_BASICS_H_INCLUDED

#include "../beast/beast/Crypto.h"

#include "../beast/modules/beast_core/system/BeforeBoost.h"
#include "system/BoostIncludes.h"

#include <atomic>
#include "../../beast/beast/cxx14/memory.h"
#include "../../beast/beast/utility/Zero.h"

using beast::zero;
using beast::Zero;

#ifndef  RIPPLE_TRACK_MUTEXES
# define RIPPLE_TRACK_MUTEXES 0
#endif

//------------------------------------------------------------------------------

#include "../ripple/types/ripple_types.h"

#include "types/BasicTypes.h"

#  include "log/LogSeverity.h"
#  include "log/LogFile.h"
# include "log/LogSink.h"
# include "log/LogPartition.h"
# include "log/Log.h"
#include "log/LoggedTimings.h"

#include "utility/CountedObject.h"
#include "utility/IniFile.h"
#include "utility/PlatformMacros.h"
#include "utility/StringUtilities.h"
#include "utility/Sustain.h"
#include "utility/ThreadName.h"
#include "utility/Time.h"
#include "utility/UptimeTimer.h"

#include "containers/RangeSet.h"
#include "containers/SyncUnorderedMap.h"

#endif
