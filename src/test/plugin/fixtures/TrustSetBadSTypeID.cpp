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

#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/ledger/View.h>
#include <ripple/plugin/exports.h>
#include <ripple/plugin/reset.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <string>

using namespace ripple;

const int STI_UINT32_2 = 23;

Buffer
parseLeafTypeNew(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    // This doesn't matter
    return Buffer();
}

std::uint32_t
bufferToUInt32(Buffer const& buf)
{
    std::uint32_t result = 0;
    int shift = 0;
    for (int i = 0; i < buf.size(); i++)
    {
        auto const byte = *(buf.data() + i);
        result |= static_cast<std::uint32_t>(byte) << shift;
        shift += 8;
    }

    return result;
}

std::string
toString(int typeId, Buffer const& buf)
{
    uint32_t val = bufferToUInt32(buf);
    return std::to_string(val);
}

void
toSerializer(int typeId, Buffer const& buf, Serializer& s)
{
    uint32_t val = bufferToUInt32(buf);
    s.add32(val);
}

Buffer
fromSerialIter(int typeId, SerialIter& st)
{
    uint32_t val = st.get32();
    return Buffer(&val, sizeof val);
}

extern "C" Container<STypeExport>
getSTypes()
{
    reinitialize();
    resetPlugins();
    static STypeExport exports[] = {
        {
            STI_UINT32_2,
            parseLeafTypeNew,
            toString,
            NULL,
            toSerializer,
            fromSerialIter,
        },
    };
    for (int i = 0; i < sizeof(exports) / sizeof(exports[0]); i++)
    {
        auto const stype = exports[i];
        registerSType(
            {stype.typeId,
             stype.toString,
             stype.toJson,
             stype.toSerializer,
             stype.fromSerialIter});
        registerLeafType(stype.typeId, stype.parsePtr);
    }
    STypeExport* ptr = exports;
    return {ptr, 1};
}

extern "C" Container<AmendmentExport>
getAmendments()
{
    AmendmentExport const amendment = {
        "featurePluginTest",
        true,
        VoteBehavior::DefaultNo,
    };
    static AmendmentExport list[] = {amendment};
    AmendmentExport* ptr = list;
    return {ptr, 1};
}
