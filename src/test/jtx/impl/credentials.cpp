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

#include <test/jtx/credentials.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace credentials {

Json::Value
create(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialCreate;

    jv[jss::Account] = issuer.human();
    jv[jss::Subject] = subject.human();

    jv[jss::Flags] = tfUniversal;
    jv[sfCredentialType.jsonName] = strHex(credType);

    return jv;
}

Json::Value
accept(
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialAccept;
    jv[jss::Account] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    jv[jss::Flags] = tfUniversal;

    return jv;
}

Json::Value
deleteCred(
    jtx::Account const& acc,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CredentialDelete;
    jv[jss::Account] = acc.human();
    jv[jss::Subject] = subject.human();
    jv[jss::Issuer] = issuer.human();
    jv[sfCredentialType.jsonName] = strHex(credType);
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& subject,
    jtx::Account const& issuer,
    std::string_view credType)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential][jss::subject] = subject.human();
    jvParams[jss::credential][jss::issuer] = issuer.human();
    jvParams[jss::credential][jss::credential_type] = strHex(credType);
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

Json::Value
ledgerEntry(jtx::Env& env, std::string const& credIdx)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::credential] = credIdx;
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace credentials

}  // namespace jtx

}  // namespace test
}  // namespace ripple
