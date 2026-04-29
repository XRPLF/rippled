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

std::tuple<json::Value, Keylet>
Vault::create(CreateArgs const& args) const
{
    auto keylet = keylet::vault(args.owner.id(), env.seq(args.owner));
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultCreate;
    jv[jss::Account] = args.owner.human();
    jv[jss::Asset] = toJson(args.asset);
    if (args.flags)
        jv[jss::Flags] = *args.flags;
    return {jv, keylet};
}

json::Value
Vault::set(SetArgs const& args)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultSet;
    jv[jss::Account] = args.owner.human();
    jv[sfVaultID] = to_string(args.id);
    return jv;
}

json::Value
Vault::del(DeleteArgs const& args)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultDelete;
    jv[jss::Account] = args.owner.human();
    jv[sfVaultID] = to_string(args.id);
    return jv;
}

json::Value
Vault::deposit(DepositArgs const& args)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultDeposit;
    jv[jss::Account] = args.depositor.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Amount] = toJson(args.amount);
    return jv;
}

json::Value
Vault::withdraw(WithdrawArgs const& args)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultWithdraw;
    jv[jss::Account] = args.depositor.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Amount] = toJson(args.amount);
    return jv;
}

json::Value
Vault::clawback(ClawbackArgs const& args)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultClawback;
    jv[jss::Account] = args.issuer.human();
    jv[sfVaultID] = to_string(args.id);
    jv[jss::Holder] = args.holder.human();
    if (args.amount)
        jv[jss::Amount] = toJson(*args.amount);
    return jv;
}

}  // namespace xrpl::test::jtx
