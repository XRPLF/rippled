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

#include <iostream>
#include <fstream>

// These are for Sustain Linux variants
#ifdef __linux__
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif
#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio.hpp> // for stupid parseIpPort
#include <boost/regex.hpp>

// VFALCO TODO Replace OpenSSL randomness with a dependency-free implementation
//         Perhaps Schneier's Fortuna or a variant. Abstract the collection of
//         entropy and provide OS-specific implementation. We can re-use the
//         BearShare source code for this.
//
//         Add Random number generation to the new VFLib
//
#include <openssl/rand.h> // Because of ripple_RandomNumbers.cpp


#include "ripple_basics.h"



// VFALCO TODO fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
//#pragma warning (disable: 4018) // signed/unsigned mismatch
//#pragma warning (disable: 4244) // conversion, possible loss of data
#endif

#include "containers/ripple_RangeSet.cpp"
#include "containers/ripple_TaggedCache.cpp"

#include "utility/ripple_Log.cpp"

#include "utility/ripple_ByteOrder.cpp"
#include "utility/ripple_DiffieHellmanUtil.cpp"
#include "utility/ripple_InstanceCounter.cpp"
#include "utility/ripple_StringUtilities.cpp"
#include "utility/ripple_Sustain.cpp"
#include "utility/ripple_ThreadName.cpp"
#include "utility/ripple_Time.cpp"
#include "utility/ripple_UptimeTimer.cpp"

#ifdef WIN32
#include <windows.h>  // for ripple_RandomNumbers.cpp
#include <wincrypt.h> // for ripple_RandomNumbers.cpp
// Winsock #defines 'max' and does other stupid things so put it last
#include <Winsock2.h> // for ripple_ByteOrder.cpp
#endif
#include "utility/ripple_RandomNumbers.cpp" // has Win32/Posix dependencies

#include "types/ripple_UInt256.cpp"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
