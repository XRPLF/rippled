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

#include <beast/Crypto.h>

#include <modules/beast_core/system/BeforeBoost.h>
#include <ripple_basics/system/BoostIncludes.h>
#include <beast/cxx14/memory.h>

#include <atomic>

#ifndef  RIPPLE_TRACK_MUTEXES
# define RIPPLE_TRACK_MUTEXES 0
#endif

//------------------------------------------------------------------------------

#include <ripple/types/ripple_types.h>

#include <ripple_basics/types/BasicTypes.h>

#include <ripple_basics/log/LogSeverity.h>
#include <ripple_basics/log/LogFile.h>
#include <ripple_basics/log/LogSink.h>
#include <ripple_basics/log/LogPartition.h>
#include <ripple_basics/log/Log.h>
#include <ripple_basics/log/LoggedTimings.h>

#include <ripple_basics/utility/CountedObject.h>
#include <ripple_basics/utility/IniFile.h>
#include <ripple_basics/utility/PlatformMacros.h>
#include <ripple_basics/utility/StringUtilities.h>
#include <ripple_basics/utility/Sustain.h>
#include <ripple_basics/utility/ThreadName.h>
#include <ripple_basics/utility/Time.h>
#include <ripple_basics/utility/UptimeTimer.h>

#include <ripple_basics/containers/RangeSet.h>
#include <ripple_basics/containers/SyncUnorderedMap.h>

#endif
