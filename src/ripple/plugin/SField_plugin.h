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

#ifndef RIPPLE_PLUGIN_SFIELD_H_INCLUDED
#define RIPPLE_PLUGIN_SFIELD_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/json/json_value.h>

namespace ripple {

struct SFieldExport
{
    int typeId;
    int fieldValue;
    const char* txtName;
};

class Serializer;
class SerialIter;

typedef std::string (*toStringPtr)(int typeId, Buffer const& buf);
typedef Json::Value (*toJsonPtr)(int typeId, Buffer const& buf);
typedef void (*toSerializerPtr)(int typeId, Buffer const& buf, Serializer& s);
typedef Buffer (*fromSerialIterPtr)(int typeId, SerialIter& st);

struct STypeFunctions
{
    int typeId;
    const char* typeName;
    toStringPtr toString;
    toJsonPtr toJson;
    toSerializerPtr toSerializer;
    fromSerialIterPtr fromSerialIter;
};

}  // namespace ripple

#endif
