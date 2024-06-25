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

#include <ripple/rpc/Status.h>
#include <sstream>

namespace ripple {
namespace RPC {

std::string
Status::codeString() const
{
    if (!*this)
        return "";

    if (type_ == Type::none)
        return std::to_string(code_);

    if (type_ == Status::Type::TER)
    {
        std::string s1, s2;

        auto success = transResultInfo(toTER(), s1, s2);
        assert(success);
        (void)success;

        return s1 + ": " + s2;
    }

    if (type_ == Status::Type::error_code_i)
    {
        auto info = get_error_info(toErrorCode());
        std::ostringstream sStr;
        sStr << info.token.c_str() << ": " << info.message.c_str();
        return sStr.str();
    }

    assert(false);
    return "";
}

void
Status::fillJson(Json::Value& value)
{
    if (!*this)
        return;

    auto& error = value[jss::error];
    error[jss::code] = code_;
    error[jss::message] = codeString();

    // Are there any more messages?
    if (!messages_.empty())
    {
        auto& messages = error[jss::data];
        for (auto& i : messages_)
            messages.append(i);
    }
}

std::string
Status::message() const
{
    std::string result;
    for (auto& m : messages_)
    {
        if (!result.empty())
            result += '/';
        result += m;
    }

    return result;
}

std::string
Status::toString() const
{
    if (*this)
        return codeString() + ":" + message();
    return "";
}

}  // namespace RPC
}  // namespace ripple
