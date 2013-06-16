//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Include this to get the @ref ripple_data module.

    @file ripple_data.h
    @ingroup ripple_data
*/

/** Ripple specific data representation and manipulation.

    These form the building blocks of Ripple data.

    @defgroup ripple_data
*/

#ifndef RIPPLE_DATA_H
#define RIPPLE_DATA_H

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <list>
#include <stdexcept>
#include <string>
#include <stdexcept>
#include <vector>

#include <boost/cstdint.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/functional/hash.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp> // VFALCO NOTE this looks like junk

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

//------------------------------------------------------------------------------

// VFALCO TODO try to reduce these dependencies
#include "../ripple_basics/ripple_basics.h"

//------------------------------------------------------------------------------

// VFALCO TODO prepare a unity header for LevelDB
// VFALCO TODO don't expose leveldb throughout the headers
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"
#include "leveldb/db.h"
#include "leveldb/write_batch.h"

// VFALCO TODO figure out a good place for this file, perhaps give it some
//         additional hierarchy via directories.
#include "ripple.pb.h"

#include "crypto/ripple_CBigNum.h"
#include "crypto/ripple_Base58.h" // VFALCO TODO Can be moved to .cpp if we clean up setAlphabet stuff
#include "crypto/ripple_Base58Data.h"

#include "protocol/ripple_FieldNames.h"
#include "protocol/ripple_HashPrefix.h"
#include "protocol/ripple_PackedMessage.h"
#include "protocol/ripple_Protocol.h"
#include "protocol/ripple_RippleAddress.h"
#include "protocol/ripple_RippleSystem.h"
#include "protocol/ripple_Serializer.h" // needs CKey
#include "protocol/ripple_TER.h"
#include "protocol/ripple_SerializedTypes.h" // needs Serializer, TER
#include "protocol/ripple_SerializedObjectTemplate.h"
#include "protocol/ripple_SerializedObject.h"
#include "protocol/ripple_LedgerFormat.h" // needs SOTemplate from SerializedObject
#include "protocol/ripple_TxFlags.h"
#include "protocol/ripple_TxFormat.h"
#include "protocol/ripple_TxFormats.h"

#include "utility/ripple_JSONCache.h"
#include "utility/ripple_UptimeTimerAdapter.h"

#endif
