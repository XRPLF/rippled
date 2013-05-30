//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

/**	Include this to get the @ref ripple_basics module.

    @file ripple_basics.h
    @ingroup ripple_basics
*/

/**	Basic classes.

	This module provides utility classes and types used in the Ripple system.

	@defgroup ripple_basics
*/

#ifndef RIPPLE_BASICS_H
#define RIPPLE_BASICS_H

#include <ctime>
#include <limits>
#include <list>
#include <sstream>
#include <string>

// UInt256
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <boost/version.hpp>
#if BOOST_VERSION < 104700
#error Boost 1.47 or later is required
#endif

// Log
#include <boost/thread/recursive_mutex.hpp>
// Forward declaration
/*
namespace boost {
	namespace filesystem {
		class path;
	}
}
*/
#include <boost/filesystem.hpp> // VFALCO: TODO, try to eliminate thie dependency



// KeyCache
#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

// RangeSet
#include <boost/foreach.hpp>
#include <boost/icl/interval_set.hpp> // oof this one is ugly

// TaggedCache
#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/make_shared.hpp>

// RippleTime
#include <boost/date_time/posix_time/posix_time.hpp>

// ScopedLock
//#include <boost/thread/recursive_mutex.hpp>
//#include <boost/shared_ptr.hpp>
//#include <boost/make_shared.hpp>
//#include <boost/ref.hpp>

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

// StringUtilities
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

// UInt256
#include <boost/functional/hash.hpp>

// VFALCO: TODO, remove this dependency!!!
#include <openssl/dh.h> // for DiffieHellmanUtil
#include <openssl/ripemd.h> // For HashUtilities
#include <openssl/sha.h> // For HashUtilities


#include "../ripple_json/ripple_json.h"

#include "utility/ripple_IntegerTypes.h" // must come first
#include "utility/ripple_Log.h" // Needed by others

#include "containers/ripple_KeyCache.h"
#include "containers/ripple_RangeSet.h"
#include "containers/ripple_SecureAllocator.h"
#include "containers/ripple_TaggedCache.h"

#include "utility/ripple_ByteOrder.h"
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

#endif
