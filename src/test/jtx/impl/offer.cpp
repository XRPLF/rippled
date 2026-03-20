#include <test/jtx/offer.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {
namespace jtx {

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
    if (flags)
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

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
