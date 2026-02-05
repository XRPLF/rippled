#pragma once

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Create an offer. */
Json::Value
offer(Account const& account, STAmount const& takerPays, STAmount const& takerGets, std::uint32_t flags = 0);

/** Cancel an offer. */
Json::Value
offer_cancel(Account const& account, std::uint32_t offerSeq);

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
