#ifndef XRPL_TEST_JTX_OFFER_H_INCLUDED
#define XRPL_TEST_JTX_OFFER_H_INCLUDED

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>

namespace ripple {
namespace test {
namespace jtx {

/** Create an offer. */
Json::Value
offer(
    Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets,
    std::uint32_t flags = 0);

/** Cancel an offer. */
Json::Value
offer_cancel(Account const& account, std::uint32_t offerSeq);

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
