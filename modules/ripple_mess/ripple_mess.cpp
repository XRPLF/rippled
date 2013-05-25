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

/**	Add this to get the @ref ripple_mess module.

    @file ripple_mess.cpp
    @ingroup ripple_mess
*/

#include "ripple_mess.h"

// VFALCO: TODO, clean this up
// This is here for Amount*.cpp
#include "src/cpp/ripple/bignum.h"
#if (ULONG_MAX > UINT_MAX)
#define BN_add_word64(bn, word) BN_add_word(bn, word)
#define BN_sub_word64(bn, word) BN_sub_word(bn, word)
#define BN_mul_word64(bn, word) BN_mul_word(bn, word)
#define BN_div_word64(bn, word) BN_div_word(bn, word)
#else
#include "src/cpp/ripple/BigNum64.h"
#endif

static const uint64 tenTo14 = 100000000000000ull;
static const uint64 tenTo14m1 = tenTo14 - 1;
static const uint64 tenTo17 = tenTo14 * 1000;
static const uint64 tenTo17m1 = tenTo17 - 1;

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#endif

#include "src/cpp/ripple/Log.cpp"

#include "src/cpp/ripple/Amount.cpp"
#include "src/cpp/ripple/AmountRound.cpp"
#include "src/cpp/ripple/BitcoinUtil.cpp" // no log
#include "src/cpp/ripple/DeterministicKeys.cpp"
#include "src/cpp/ripple/ECIES.cpp" // no log
#include "src/cpp/ripple/FieldNames.cpp" // no log
#include "src/cpp/ripple/HashedObject.cpp"
#include "src/cpp/ripple/PackedMessage.cpp" // no log
#include "src/cpp/ripple/ParameterTable.cpp" // no log
#include "src/cpp/ripple/ParseSection.cpp"
#include "src/cpp/ripple/PlatRand.cpp" // no log
#include "src/cpp/ripple/ProofOfWork.cpp"
#include "src/cpp/ripple/RangeSet.cpp"
#include "src/cpp/ripple/RippleAddress.cpp"
#include "src/cpp/ripple/rfc1751.cpp" // no log
#include "src/cpp/ripple/SHAMap.cpp"
#include "src/cpp/ripple/SHAMapDiff.cpp" // no log
#include "src/cpp/ripple/SHAMapNodes.cpp" // no log
#include "src/cpp/ripple/SHAMapSync.cpp"
#include "src/cpp/ripple/utils.cpp" // no log

#include "ripple.pb.cc"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
