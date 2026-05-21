#include <test/jtx/offer.h>

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx {

json::Value
offer(
    Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets,
    std::uint32_t flags)
{
    json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TakerPays] = takerPays.getJson(JsonOptions::Values::None);
    jv[jss::TakerGets] = takerGets.getJson(JsonOptions::Values::None);
    if (flags != 0u)
        jv[jss::Flags] = flags;
    jv[jss::TransactionType] = jss::OfferCreate;
    return jv;
}

json::Value
offerCancel(Account const& account, std::uint32_t offerSeq)
{
    json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::OfferSequence] = offerSeq;
    jv[jss::TransactionType] = jss::OfferCancel;
    return jv;
}

}  // namespace xrpl::test::jtx
