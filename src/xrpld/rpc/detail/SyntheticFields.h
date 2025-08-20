#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>

namespace xrpl {

class ReadView;

namespace RPC {

struct JsonContext;

/**
 * Adds all synthetic fields to transaction metadata JSON.
 * This includes delivered amount, NFT synthetic fields, and MPToken issuance
 * ID.
 */
/** @{ */
void
insertAllSyntheticInJson(
    json::Value& metadata,
    ReadView const&,
    std::shared_ptr<STTx const> const&,
    TxMeta const&);

void
insertAllSyntheticInJson(
    json::Value& metadata,
    JsonContext const&,
    std::shared_ptr<STTx const> const&,
    TxMeta const&);
/** @} */

}  // namespace RPC
}  // namespace xrpl
