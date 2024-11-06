//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

namespace credentials {

// Sets the optional URI.
class uri
{
private:
    std::string const uri_;

public:
    explicit uri(std::string_view u) : uri_(strHex(u))
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        jtx.jv[sfURI.jsonName] = uri_;
    }
};

// Set credentialsIDs array
class ids
{
private:
    std::vector<std::string> const credentials_;

public:
    explicit ids(std::vector<std::string> const& creds) : credentials_(creds)
    {
    }

    void
    operator()(jtx::Env&, jtx::JTx& jtx) const
    {
        auto& arr(jtx.jv[sfCredentialIDs.jsonName] = Json::arrayValue);
        for (auto const& hash : credentials_)
            arr.append(hash);
    }
};

Json::Value
create(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
accept(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType);

Json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx);

}  // namespace credentials

}  // namespace jtx

}  // namespace test
}  // namespace ripple
