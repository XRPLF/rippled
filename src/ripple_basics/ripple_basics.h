//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Include this to get the @ref ripple_basics module.

    @file ripple_basics.h
    @ingroup ripple_basics
*/

/** Basic classes.

    This module provides utility classes and types used in the Ripple system.

    @defgroup ripple_basics
*/

#ifndef RIPPLE_BASICS_RIPPLEHEADER
#define RIPPLE_BASICS_RIPPLEHEADER

#include "system/StandardIncludes.h"

//------------------------------------------------------------------------------

// For json/
//
// VFALCO TODO Clean up these one-offs
#include "json/json_config.h" // Needed before these cpptl includes
#ifndef JSON_USE_CPPTL_SMALLMAP
# include <map>
#else
# include <cpptl/smallmap.h>
#endif
#ifdef JSON_USE_CPPTL
# include <cpptl/forwards.h>
#endif

//------------------------------------------------------------------------------

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include "system/BoostIncludes.h"

#include "system/OpenSSLIncludes.h"

#include "beast/modules/beast_crypto/beast_crypto.h"

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

//------------------------------------------------------------------------------

// ByteOrder
#if BEAST_WIN32
// (nothing)
#elif __APPLE__
# include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
# include <sys/endian.h>
#elif defined(__OpenBSD__)
# include <sys/types.h>
#endif

#include "beast/modules/beast_core/beast_core.h"

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

#include "utility/ByteOrder.h"
#include "utility/CountedObject.h"
#include "utility/DiffieHellmanUtil.h"
#include "utility/IniFile.h"
#include "utility/PlatformMacros.h"
#include "utility/RandomNumbers.h"
#include "utility/Service.h"
#include "utility/StringUtilities.h"
#include "utility/Sustain.h"
#include "utility/ThreadName.h"
#include "utility/Time.h"
#include "utility/UptimeTimer.h"

#include "types/UInt256.h"
#include "utility/HashUtilities.h" // requires UInt256
#include "types/HashMaps.h"

#include "containers/KeyCache.h"
#include "containers/RangeSet.h"
#include "containers/SecureAllocator.h"
#include "containers/TaggedCache.h"

#include "json/json_forwards.h"
#include "json/json_features.h"
#include "json/json_value.h"
#include "json/json_reader.h"
#include "json/json_writer.h"

}

#endif
