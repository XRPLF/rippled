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

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include <boost/version.hpp>
#if BOOST_VERSION < 104700
#error Ripple requires Boost version 1.47 or later
#endif

// VFALCO TODO Move all boost includes into ripple_BoostHeaders.h
//
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>

// ByteOrder
#ifdef WIN32
// (nothing)
#elif __APPLE__
# include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
# include <sys/endian.h>
#elif defined(__OpenBSD__)
# include <sys/types.h>
#endif

#include <openssl/dh.h> // for DiffieHellmanUtil
#include <openssl/ripemd.h> // For HashUtilities
#include <openssl/sha.h> // For HashUtilities

#include "BeastConfig.h" // Must come before any Beast includes

#include "modules/beast_core/beast_core.h"
#include "modules/beast_basics/beast_basics.h"

#include "../ripple_json/ripple_json.h"

#if RIPPLE_USE_NAMESPACE
namespace ripple
{
#endif

#include "utility/ripple_IntegerTypes.h" // must come first
#include "utility/ripple_Log.h" // Needed by others

#include "types/ripple_BasicTypes.h"
#include "utility/ripple_ByteOrder.h"
#include "utility/ripple_CountedObject.h"
#include "utility/ripple_DiffieHellmanUtil.h"
#include "utility/ripple_PlatformMacros.h"
#include "utility/ripple_RandomNumbers.h"
#include "utility/ripple_ScopedLock.h"
#include "utility/ripple_StringUtilities.h"
#include "utility/ripple_Sustain.h"
#include "utility/ripple_ThreadName.h"
#include "utility/ripple_Time.h"
#include "utility/ripple_UptimeTimer.h"

#include "types/ripple_UInt256.h"
#include "utility/ripple_HashUtilities.h" // requires UInt256
#include "types/ripple_HashMaps.h"

#include "containers/ripple_KeyCache.h"
#include "containers/ripple_RangeSet.h"
#include "containers/ripple_SecureAllocator.h"
#include "containers/ripple_TaggedCache.h"

#if RIPPLE_USE_NAMESPACE
}
#endif

#endif
