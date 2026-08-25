#include <test/jtx/sponsor.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>

namespace xrpl::test::jtx::sponsor {

json::Value
set(jtx::Account const& account,
    uint32_t flags,
    std::optional<uint32_t> const reserveCount,
    std::optional<STAmount> const feeAmount,
    std::optional<STAmount> const maxFee)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    if (reserveCount)
        jv[sfRemainingOwnerCount.jsonName] = *reserveCount;
    if (feeAmount)
        jv[sfFeeAmount.jsonName] = feeAmount->getJson(JsonOptions::Values::None);
    if (maxFee)
        jv[sfMaxFee.jsonName] = maxFee->getJson(JsonOptions::Values::None);
    return jv;
}

json::Value
del(jtx::Account const& account)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = tfDeleteObject;
    return jv;
}

json::Value
transfer(jtx::Account const& account, uint32_t flags, std::optional<uint256> const& index)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipTransfer;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    if (index)
        jv[sfObjectID.jsonName] = to_string(*index);
    return jv;
}

void
CounterpartySponsor::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfCounterpartySponsor.jsonName] = sponsor_.human();
}

void
SponseeAcc::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsee.jsonName] = sponsee_.human();
}

void
As::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsor.jsonName] = sponsor_.human();
    jt.jv[sfSponsorFlags.jsonName] = flags_;
}

json::Value
ledgerEntry(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::sponsorship][jss::sponsor] = sponsor.human();
    jvParams[jss::sponsorship][jss::sponsee] = sponsee.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

STAmount
sponsorshipFeeBalance(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee)
{
    return env.le(keylet::sponsorship(sponsor, sponsee))->getFieldAmount(sfFeeAmount).xrp();
}

}  // namespace xrpl::test::jtx::sponsor
