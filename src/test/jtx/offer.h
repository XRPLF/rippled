#pragma once

#include <test/jtx/Account.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>

namespace xrpl::test::jtx {

/** Create an offer. */
json::Value
offer(
    Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets,
    std::uint32_t flags = 0);

/** Cancel an offer. */
json::Value
offerCancel(Account const& account, std::uint32_t offerSeq);

}  // namespace xrpl::test::jtx
