#include <test/jtx/sponsor.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>

namespace xrpl::test::jtx::sponsor {

Json::Value
set(jtx::Account const& account,
    uint32_t flags,
    std::optional<uint32_t> reserveCount,
    std::optional<STAmount> feeAmount,
    std::optional<STAmount> maxFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    if (reserveCount)
        jv[sfReserveCount.jsonName] = *reserveCount;
    if (feeAmount)
        jv[sfFeeAmount.jsonName] = feeAmount->getJson(JsonOptions::none);
    if (maxFee)
        jv[sfMaxFee.jsonName] = maxFee->getJson(JsonOptions::none);
    return jv;
}

Json::Value
set_fee(
    jtx::Account const& account,
    uint32_t flags,
    STAmount feeAmount,
    std::optional<STAmount> maxFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    jv[sfFeeAmount.jsonName] = feeAmount.getJson(JsonOptions::none);
    if (maxFee)
        jv[sfMaxFee.jsonName] = maxFee->getJson(JsonOptions::none);
    return jv;
}

Json::Value
set_reserve(jtx::Account const& account, uint32_t flags, uint32_t reserveCount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    jv[sfReserveCount.jsonName] = reserveCount;
    return jv;
}

Json::Value
set_max_fee(jtx::Account const& account, uint32_t flags, STAmount maxFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    jv[sfMaxFee.jsonName] = maxFee.getJson(JsonOptions::none);
    return jv;
}

Json::Value
del(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = tfDeleteObject;
    return jv;
}

Json::Value
transfer(jtx::Account const& account, uint32_t flags, std::optional<uint256> const& index)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipTransfer;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    if (index)
        jv[sfObjectID.jsonName] = to_string(*index);
    return jv;
}

void
counterpartySponsor::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfCounterpartySponsor.jsonName] = sponsor_.human();
}

void
sponseeAcc::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsee.jsonName] = sponsee_.human();
}

void
as::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsor.jsonName] = sponsor_.human();
    jt.jv[sfSponsorFlags.jsonName] = flags;
}

Json::Value
ledgerEntry(jtx::Env& env, jtx::Account const& sponsor, jtx::Account const& sponsee)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::sponsorship][jss::sponsor] = sponsor.human();
    jvParams[jss::sponsorship][jss::sponsee] = sponsee.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace xrpl::test::jtx::sponsor
