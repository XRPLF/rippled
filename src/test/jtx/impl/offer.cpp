#include <test/jtx/offer.h>

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx {

Json::Value
offer(
    Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets,
    std::uint32_t flags)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TakerPays] = takerPays.getJson(JsonOptions::kNONE);
    jv[jss::TakerGets] = takerGets.getJson(JsonOptions::kNONE);
    if (flags != 0u)
        jv[jss::Flags] = flags;
    jv[jss::TransactionType] = jss::OfferCreate;
    return jv;
}

Json::Value
offerCancel(Account const& account, std::uint32_t offerSeq)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::OfferSequence] = offerSeq;
    jv[jss::TransactionType] = jss::OfferCancel;
    return jv;
}

}  // namespace xrpl::test::jtx
