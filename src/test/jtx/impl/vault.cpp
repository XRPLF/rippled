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

#include <test/jtx/Env.h>
#include <test/jtx/vault.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace ripple {
namespace test {
namespace jtx {

std::tuple<Json::Value, Keylet>
Vault::create(CreateArgs const& args)
{
    auto keylet = keylet::vault(args.owner.id(), env.seq(args.owner));
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultCreate;
    jv[jss::Account] = args.owner.human();
    jv[jss::Asset] = to_json(args.asset);
    if (args.flags)
        jv[jss::Flags] = *args.flags;
    return {jv, keylet};
}

Json::Value
Vault::set(SetArgs const& args)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultSet;
    jv[jss::Account] = args.owner.human();
    jv[sfVaultID] = to_string(args.id);
    return jv;
}

Json::Value
Vault::del(DeleteArgs const& args)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultDelete;
    jv[jss::Account] = args.owner.human();
    jv[sfVaultID] = to_string(args.id);
    return jv;
}

Json::Value
Vault::deposit(DepositArgs const& args)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultDeposit;
    jv[jss::Account] = args.depositor.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Amount] = to_json(args.amount);
    return jv;
}

Json::Value
Vault::withdraw(WithdrawArgs const& args)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultWithdraw;
    jv[jss::Account] = args.depositor.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Amount] = to_json(args.amount);
    return jv;
}

Json::Value
Vault::clawback(ClawbackArgs const& args)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::VaultClawback;
    jv[jss::Account] = args.issuer.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Holder] = args.holder.human();
    if (args.amount)
        jv[jss::Amount] = to_json(*args.amount);
    return jv;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
