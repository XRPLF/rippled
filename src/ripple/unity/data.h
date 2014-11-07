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

#include <ripple/unity/types.h>
#include <ripple/unity/json.h>
#include <ripple/sslutil/ECDSACanonical.h>

struct bignum_st;
typedef struct bignum_st BIGNUM;

#include <ripple/crypto/Base58Data.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Serializer.h> // needs CKey
#include <ripple/protocol/TER.h>
#include <ripple/protocol/SerializedTypes.h> // needs Serializer, TER
#include <ripple/protocol/SerializedObjectTemplate.h>
#include <ripple/protocol/KnownFormats.h>
#include <ripple/protocol/LedgerFormats.h> // needs SOTemplate from SerializedObjectTemplate
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/TxFlags.h>

#include <ripple/protocol/STParsedJSON.h>

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
