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

#include "system/ripple_StandardIncludes.h"

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

// Must come before <boost/bind.hpp>
#include "beast/modules/beast_core/beast_core.h"

#include "system/ripple_BoostIncludes.h"

#include "system/ripple_OpenSSLIncludes.h"

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

#include "types/ripple_BasicTypes.h"

#include "utility/ripple_LogFile.h"
#include "utility/ripple_Log.h" // Needed by others

#include "utility/ripple_ByteOrder.h"
#include "utility/ripple_CountedObject.h"
#include "utility/ripple_DiffieHellmanUtil.h"
#include "utility/ripple_IniFile.h"
#include "utility/ripple_PlatformMacros.h"
#include "utility/ripple_RandomNumbers.h"
#include "utility/ripple_StringUtilities.h"
#include "utility/ripple_Sustain.h"
#include "utility/ripple_ThreadName.h"
#include "utility/ripple_Time.h"
#include "utility/ripple_LoggedTimings.h"
#include "utility/ripple_UptimeTimer.h"

#include "types/ripple_UInt256.h"
#include "utility/ripple_HashUtilities.h" // requires UInt256
#include "types/ripple_HashMaps.h"

#include "containers/ripple_KeyCache.h"
#include "containers/ripple_RangeSet.h"
#include "containers/ripple_SecureAllocator.h"
#include "containers/ripple_TaggedCache.h"

#include "json/json_forwards.h"
#include "json/json_features.h"
#include "json/json_value.h"
#include "json/json_reader.h"
#include "json/json_writer.h"

}

#endif
