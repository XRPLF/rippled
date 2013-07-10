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

// VFALCO TODO try to reduce these dependencies
#include "../ripple_basics/ripple_basics.h"

// VFALCO TODO don't expose leveldb throughout the headers
#include "../ripple_leveldb/ripple_leveldb.h"

// VFALCO TODO figure out a good place for this file, perhaps give it some
//         additional hierarchy via directories.
#include "ripple.pb.h"

namespace ripple
{

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
 #include "protocol/ripple_KnownFormats.h"
 #include "protocol/ripple_LedgerFormats.h" // needs SOTemplate from SerializedObjectTemplate
 #include "protocol/ripple_TxFormats.h"
#include "protocol/ripple_SerializedObject.h"
#include "protocol/ripple_TxFlags.h"

#include "utility/ripple_UptimeTimerAdapter.h"

}

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

#endif
