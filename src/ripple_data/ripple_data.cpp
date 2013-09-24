//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_data.h"

//#include <cmath>

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/copied.hpp>
#include <boost/regex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/err.h>

#include "../ripple/sslutil/ripple_sslutil.h"

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

#include "crypto/CKey.h" // needs RippleAddress VFALCO TODO remove this dependency cycle
#include "crypto/RFC1751.h"

#include "crypto/CKey.cpp"
#include "crypto/CKeyDeterministic.cpp"
#include "crypto/CKeyECIES.cpp"
#include "crypto/Base58Data.cpp"
#include "crypto/RFC1751.cpp"

#include "protocol/BuildInfo.cpp"
#include "protocol/FieldNames.cpp"
#include "protocol/LedgerFormats.cpp"
#include "protocol/RippleAddress.cpp"
#include "protocol/SerializedTypes.cpp"
#include "protocol/Serializer.cpp"
#include "protocol/SerializedObjectTemplate.cpp"
#include "protocol/SerializedObject.cpp"
#include "protocol/TER.cpp"
#include "protocol/TxFormats.cpp"

// These are for STAmount
static const uint64 tenTo14 = 100000000000000ull;
static const uint64 tenTo14m1 = tenTo14 - 1;
static const uint64 tenTo17 = tenTo14 * 1000;
static const uint64 tenTo17m1 = tenTo17 - 1;
#include "protocol/STAmount.cpp"
#include "protocol/STAmountRound.cpp"

}

#if BEAST_MSVC
#pragma warning (pop)
#endif
