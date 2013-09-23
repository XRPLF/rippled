//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TYPES_H_INCLUDED
#define RIPPLE_TYPES_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"
#include "beast/modules/beast_crypto/beast_crypto.h" // for UnsignedInteger, Remove ASAP!

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/functional/hash.hpp>

// For ByteOrder
#if BEAST_WIN32
// (nothing)
#elif __APPLE__
# include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
# include <sys/endian.h>
#elif defined(__OpenBSD__)
# include <sys/types.h>
#endif

namespace ripple {
using namespace beast;
}

#  include "api/Blob.h"
# include "api/Base58.h"
#  include "api/ByteOrder.h"
#  include "api/strHex.h"
# include "api/UInt128.h"
# include "api/UInt160.h"
# include "api/UInt256.h"
# include "api/RandomNumbers.h"
#include "api/HashMaps.h"
#include "api/CryptoIdentifierType.h"
# include "api/CryptoIdentifierStorage.h"

#include "api/RippleAccountID.h"
#include "api/RippleAccountPublicKey.h"
#include "api/RippleAccountPrivateKey.h"
#include "api/RipplePublicKey.h"
#include "api/RipplePrivateKey.h"

#include "api/RipplePublicKeyHash.h"

#endif
