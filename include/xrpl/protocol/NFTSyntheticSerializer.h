#pragma once

#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>

namespace xrpl {

namespace RPC {

/**
   Adds common synthetic fields to transaction-related JSON responses

   @{
 */
void
insertNFTSyntheticInJson(Json::Value&, std::shared_ptr<STTx const> const&, TxMeta const&);
/** @} */

}  // namespace RPC
}  // namespace xrpl
