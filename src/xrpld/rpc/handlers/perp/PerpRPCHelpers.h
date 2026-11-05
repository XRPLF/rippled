#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/STVector256.h>

#include <optional>

namespace xrpl::rpc {

/**
 * Parses a request member naming a ledger entry by its 256-bit id in hex.
 *
 * @param params The request.
 * @param field The member to parse.
 * @param result The response, given rpcINVALID_PARAMS when the member is missing or not hex.
 * @return The id, or nullopt after the error is injected.
 */
[[nodiscard]] std::optional<uint256>
parsePerpID(json::Value const& params, json::StaticString const& field, json::Value& result);

/**
 * Refreshes markets in view as the next transaction of its ledger would (spec 5.4), so that reads
 * of view see each market's live index, status and funding. A missing market or exchange is
 * skipped.
 */
void
refreshPerpMarkets(ApplyView& view, STVector256 const& marketIDs, beast::Journal j);

}  // namespace xrpl::rpc
