//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_H_INCLUDED
#define RIPPLE_BASICS_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"
#include "beast/modules/beast_crypto/beast_crypto.h"

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include "system/BoostIncludes.h"

#include "../../beast/beast/Utility.h"

#ifndef RIPPLE_TRACK_MUTEXES
# define RIPPLE_TRACK_MUTEXES 0
#endif

//------------------------------------------------------------------------------

// From
// http://stackoverflow.com/questions/4682343/how-to-resolve-conflict-between-boostshared-ptr-and-using-stdshared-ptr
//
#if __cplusplus > 201100L
namespace boost
{
    template <class T>
    const T* get_pointer (std::shared_ptr<T> const& ptr)
    {
        return ptr.get();
    }

    template <class T>
    T* get_pointer (std::shared_ptr<T>& ptr)
    {
        return ptr.get();
    }
}
#endif

#include "../ripple/types/ripple_types.h"

namespace ripple
{

using namespace beast;

#include "types/BasicTypes.h"

#  include "log/LogSeverity.h"
#  include "log/LogFile.h"
# include "log/LogSink.h"
# include "log/LogPartition.h"
# include "log/Log.h"
#include "log/LogJournal.h"
#include "log/LoggedTimings.h"

#include "utility/CountedObject.h"
#include "utility/IniFile.h"
#include "utility/PlatformMacros.h"
#include "utility/StringUtilities.h"
#include "utility/Sustain.h"
#include "utility/ThreadName.h"
#include "utility/Time.h"
#include "utility/UptimeTimer.h"

#include "containers/KeyCache.h"
#include "containers/RangeSet.h"
#include "containers/TaggedCache.h"

}

#endif
