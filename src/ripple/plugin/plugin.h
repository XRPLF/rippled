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

#ifndef RIPPLE_PLUGIN_PLUGIN_H_INCLUDED
#define RIPPLE_PLUGIN_PLUGIN_H_INCLUDED

#include <ripple/plugin/SField.h>
#include <ripple/protocol/SOTemplate.h>
#include <vector>

namespace ripple {

class STLedgerEntry;
enum class VoteBehavior;

template <typename T>
struct Container
{
    T* data;
    int size;
};

struct SOElementExport
{
    int fieldCode;
    SOEStyle style;
};

struct TERExport
{
    int code;
    char const* codeStr;
    char const* description;
};

typedef Buffer (*parsePluginValuePtr)(
    SField const&,
    std::string const&,
    std::string const&,
    SField const*,
    Json::Value const&,
    Json::Value&);

struct STypeExport
{
    int typeId;
    parsePluginValuePtr parsePtr;
    toStringPtr toString;
    toJsonPtr toJson;
    toSerializerPtr toSerializer;
    fromSerialIterPtr fromSerialIter;
};

struct AmendmentExport
{
    char const* name;
    bool supported;
    VoteBehavior vote;
};

struct InnerObjectExport
{
    std::uint16_t code;
    char const* name;
    Container<SOElementExport> format;
};

}  // namespace ripple

#endif
