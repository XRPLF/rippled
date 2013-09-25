//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_DATA_H_INCLUDED
#define RIPPLE_DATA_H_INCLUDED

#include "../ripple_basics/ripple_basics.h"
#include "../ripple/json/ripple_json.h"

struct bignum_st;
typedef struct bignum_st BIGNUM;

namespace ripple {

#include "crypto/Base58Data.h"
#include "crypto/RFC1751.h"

#include "protocol/BuildInfo.h"
#include "protocol/FieldNames.h"
#include "protocol/HashPrefix.h"
#include "protocol/Protocol.h"
#include "protocol/RippleAddress.h"
#include "protocol/RippleSystem.h"
#include "protocol/Serializer.h" // needs CKey
#include "protocol/TER.h"
#include "protocol/SerializedTypes.h" // needs Serializer, TER
#include "protocol/SerializedObjectTemplate.h"
 #include "protocol/KnownFormats.h"
 #include "protocol/LedgerFormats.h" // needs SOTemplate from SerializedObjectTemplate
 #include "protocol/TxFormats.h"
#include "protocol/SerializedObject.h"
#include "protocol/TxFlags.h"

#include "utility/UptimeTimerAdapter.h"

}

//------------------------------------------------------------------------------

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
