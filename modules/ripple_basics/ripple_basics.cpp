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

/**	Add this to get the @ref ripple_basics module.

    @file ripple_basics.cpp
    @ingroup ripple_basics
*/

#include "ripple_basics.h"

#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio.hpp> // for stupid parseIpPort
#include <boost/regex.hpp>

// VFALCO: TODO, Replace OpenSSL randomness with a dependency-free implementation
//         Perhaps Schneier's Yarrow or a variant. Abstract the collection of
//         entropy and provide OS-specific implementation. We can re-use the
//         BearShare source code for this.
//
//         Add Random number generation to the new VFLib
//
#include <openssl/rand.h> // Because of ripple_RandomNumbers.cpp

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
//#pragma warning (disable: 4018) // signed/unsigned mismatch
//#pragma warning (disable: 4244) // conversion, possible loss of data
#endif

#include "containers/ripple_RangeSet.cpp"

#include "diagnostic/ripple_Log.cpp"

#include "events/ripple_UptimeTimer.cpp"

#ifdef WIN32
// Winsock #defines 'max' and does other stupid things so put it last
#include "Winsock2.h" // for ripple_ByteOrder.cpp
#endif
#include "memory/ripple_ByteOrder.cpp"
#include "memory/ripple_StringUtilities.cpp"

#include "system/ripple_RandomNumbers.cpp"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
