//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_DATA_H_INCLUDED
#define RIPPLE_DATA_H_INCLUDED

#include "../ripple_basics/ripple_basics.h"
#include "../ripple/json/ripple_json.h"
#include "../ripple/sslutil/api/ECDSACanonical.h"

struct bignum_st;
typedef struct bignum_st BIGNUM;

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

#include "protocol/STParsedJSON.h"

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
