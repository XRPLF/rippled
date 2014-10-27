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

#include <ripple/rpc/impl/WriteJson.h>
#include <ripple/rpc/impl/JsonWriter.h>
#include <ripple/basics/Join.h>

namespace ripple {
namespace RPC {
namespace New {

namespace {

void write (Json::Value const& value, Writer& writer)
{
    switch (value.type())
    {
    case Json::nullValue:
    {
        writer.output (nullptr);
        break;
    }

    case Json::intValue:
    {
        writer.output (value.asInt());
        break;
    }

    case Json::uintValue:
    {
        writer.output (value.asUInt());
        break;
    }

    case Json::realValue:
    {
        writer.output (value.asDouble());
        break;
    }

    case Json::stringValue:
    {
        writer.output (value.asString());
        break;
    }

    case Json::booleanValue:
    {
        writer.output (value.asBool());
        break;
    }

    case Json::arrayValue:
    {
        writer.startRoot (Writer::array);
        for (auto const& i: value)
        {
            writer.rawAppend();
            write (i, writer);
        }
        writer.finish();
        break;
    }

    case Json::objectValue:
    {
        writer.startRoot (Writer::object);
        auto members = value.getMemberNames ();
        for (auto const& tag: members)
        {
            writer.rawSet (tag);
            write (value[tag], writer);
        }
        writer.finish();
        break;
    }
    } // switch
}

struct VectorOutput : public Output
{
    void output (char const* data, size_t length) override
    {
        parts.emplace_back (data, length);
    }

    std::vector<std::string> parts;
};

} // namespace

void write (Json::Value const& value, Output& out)
{
    Writer writer (out);
    write (value, writer);
}

std::string to_string (Json::Value& value)
{
    VectorOutput out;
    Writer writer (out);
    write (value, writer);
    return join (out.parts, "");
}

} // New
} // RPC
} // ripple
