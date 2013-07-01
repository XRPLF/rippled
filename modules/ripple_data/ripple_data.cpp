//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_data module.

    @file ripple_data.cpp
    @ingroup ripple_data
*/

#include "BeastConfig.h"

#include "ripple_data.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <map>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptor/copied.hpp>
#include <boost/regex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
//#include <openssl/rand.h> // includes <windows.h> and causes errors due to #define GetMessage
#include <openssl/err.h>

// VFALCO TODO fix these warnings!
#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4018) // signed/unsigned mismatch
#endif

#ifdef min
#undef min
#endif

namespace ripple
{

#include "crypto/ripple_Base58.h" // for RippleAddress
#include "crypto/ripple_CKey.h" // needs RippleAddress VFALCO TODO remove this dependency cycle
#include "crypto/ripple_RFC1751.h"

#include "crypto/ripple_CBigNum.cpp"
#include "crypto/ripple_CKey.cpp"
#include "crypto/ripple_CKeyDeterministic.cpp"
#include "crypto/ripple_CKeyECIES.cpp"
#include "crypto/ripple_Base58.cpp"
#include "crypto/ripple_Base58Data.cpp"
#include "crypto/ripple_RFC1751.cpp"

#include "protocol/ripple_FieldNames.cpp"
#include "protocol/ripple_LedgerFormat.cpp"
#include "protocol/ripple_PackedMessage.cpp"
#include "protocol/ripple_RippleAddress.cpp"
#include "protocol/ripple_SerializedTypes.cpp"
#include "protocol/ripple_Serializer.cpp"
#include "protocol/ripple_SerializedObjectTemplate.cpp"
#include "protocol/ripple_SerializedObject.cpp"
#include "protocol/ripple_TER.cpp"
#include "protocol/ripple_TxFormats.cpp"

// These are for STAmount
static const uint64 tenTo14 = 100000000000000ull;
static const uint64 tenTo14m1 = tenTo14 - 1;
static const uint64 tenTo17 = tenTo14 * 1000;
static const uint64 tenTo17m1 = tenTo17 - 1;
#include "protocol/ripple_STAmount.cpp"
#include "protocol/ripple_STAmountRound.cpp"

}

// These must be outside the namespace because of boost
#include "crypto/ripple_CKeyDeterministicUnitTests.cpp"
#include "protocol/ripple_RippleAddressUnitTests.cpp"
#include "protocol/ripple_SerializedObjectUnitTests.cpp"
#include "protocol/ripple_SerializerUnitTests.cpp"
#include "protocol/ripple_STAmountUnitTests.cpp"

// VFALCO TODO Fix this for SConstruct
#if BEAST_MSVC
#include "ripple.pb.cc"
#endif

#if BEAST_MSVC
#pragma warning (pop)
#endif
