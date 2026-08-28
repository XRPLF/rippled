#include <test/jtx/vault.h>

#include <test/jtx/Env.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl::test::jtx {

std::tuple<json::Value, Keylet>
Vault::create(CreateArgs const& args) const
{
    auto const seqProxy = SeqProxy::rawSequence(env.seq(args.owner));
    auto keylet = keylet::vault(args.owner.id(), seqProxy);
    json::Value jv;
    jv[jss::TransactionType] = jss::VaultCreate;
    jv[jss::Account] = args.owner.human();
    jv[jss::Asset] = toJson(args.asset);
    if (args.flags)
        jv[jss::Flags] = *args.flags;
    if (args.vaultKind)
        jv[sfVaultKind] = *args.vaultKind;
    if (args.subscriptionDate)
        jv[sfSubscriptionDate] = *args.subscriptionDate;
    if (args.redemptionDate)
        jv[sfRedemptionDate] = *args.redemptionDate;
    return {jv, keylet};
}

std::tuple<json::Value, Keylet, NetClock::time_point>
Vault::createClosedEnded(CreateClosedEndedArgs const& args) const
{
    auto const sub = env.now() + args.subscriptionOffset;
    auto const red = sub + args.investmentWindow;
    auto [jv, keylet] = create(
        {.owner = args.owner,
         .asset = args.asset,
         .flags = args.flags,
         .vaultKind = std::to_underlying(VaultKind::ClosedEnded),
         .subscriptionDate = static_cast<std::uint32_t>(sub.time_since_epoch().count()),
         .redemptionDate = static_cast<std::uint32_t>(red.time_since_epoch().count())});
    return {jv, keylet, sub};
}

void
Vault::closePastSubscription(NetClock::time_point subscriptionDate) const
{
    env.close(subscriptionDate + std::chrono::seconds{1});
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
