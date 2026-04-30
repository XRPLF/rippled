#include <test/jtx/vault.h>

#include <test/jtx/Env.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/jss.h>

#include <optional>
#include <tuple>

namespace xrpl::test::jtx {

std::tuple<Json::Value, Keylet>
Vault::create(CreateArgs const& args) const
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

}  // namespace xrpl::test::jtx
