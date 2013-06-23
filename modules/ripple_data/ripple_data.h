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

#ifndef RIPPLE_DATA_RIPPLEHEADER
#define RIPPLE_DATA_RIPPLEHEADER

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

// VFALCO TODO don't expose leveldb throughout the headers
#include "../ripple_leveldb/ripple_leveldb.h"

// VFALCO TODO figure out a good place for this file, perhaps give it some
//         additional hierarchy via directories.
#include "ripple.pb.h"

#if RIPPLE_USE_NAMESPACE
namespace ripple
{
#endif

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

#if RIPPLE_USE_NAMESPACE
}
#endif

#if RIPPLE_USE_NAMESPACE
namespace boost
{
    template <>
    struct range_mutable_iterator <ripple::STPath>
    {
        typedef std::vector <ripple::STPathElement>::iterator type;
    };

    template <>
    struct range_const_iterator <ripple::STPath>
    {
        typedef std::vector <ripple::STPathElement>::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <ripple::STPathSet>
    {
        typedef std::vector <ripple::STPath>::iterator type;
    };

    template <>
    struct range_const_iterator <ripple::STPathSet>
    {
        typedef std::vector <ripple::STPath>::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <ripple::STObject>
    {
        typedef ripple::STObject::iterator type;
    };

    template <>
    struct range_const_iterator <ripple::STObject>
    {
        typedef ripple::STObject::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <ripple::STArray>
    {
        typedef ripple::STArray::iterator type;
    };

    template <>
    struct range_const_iterator <ripple::STArray>
    {
        typedef ripple::STArray::const_iterator type;
    };
}
#else
namespace boost
{
    template <>
    struct range_mutable_iterator <STPath>
    {
        typedef std::vector <STPathElement>::iterator type;
    };

    template <>
    struct range_const_iterator <STPath>
    {
        typedef std::vector <STPathElement>::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <STPathSet>
    {
        typedef std::vector <STPath>::iterator type;
    };

    template <>
    struct range_const_iterator <STPathSet>
    {
        typedef std::vector <STPath>::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <STObject>
    {
        typedef STObject::iterator type;
    };

    template <>
    struct range_const_iterator <STObject>
    {
        typedef STObject::const_iterator type;
    };

    template <>
    struct range_mutable_iterator <STArray>
    {
        typedef STArray::iterator type;
    };

    template <>
    struct range_const_iterator <STArray>
    {
        typedef STArray::const_iterator type;
    };
}
#endif

#endif
