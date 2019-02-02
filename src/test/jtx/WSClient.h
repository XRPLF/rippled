//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_WSCLIENT_H_INCLUDED
#define RIPPLE_TEST_WSCLIENT_H_INCLUDED

#include <test/jtx/AbstractClient.h>
#include <ripple/core/Config.h>
#include <boost/optional.hpp>
#include <chrono>
#include <memory>

namespace ripple {
namespace test {

class WSClient : public AbstractClient
{
public:
    /** Retrieve a message. */
    virtual
    boost::optional<Json::Value>
    getMsg(std::chrono::milliseconds const& timeout =
        std::chrono::milliseconds{0}) = 0;

    /** Retrieve a message that meets the predicate criteria. */
    virtual
    boost::optional<Json::Value>
    findMsg(std::chrono::milliseconds const& timeout,
        std::function<bool(Json::Value const&)> pred) = 0;
};

/** Returns a client operating through WebSockets/S. */
std::unique_ptr<WSClient>
makeWSClient(Config const& cfg, bool v2 = true, unsigned rpc_version = 2,
    std::unordered_map<std::string, std::string> const& headers = {});

} // test
} // ripple

#endif
