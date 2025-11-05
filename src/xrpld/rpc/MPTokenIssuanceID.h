#ifndef XRPL_RPC_MPTOKENISSUANCEID_H_INCLUDED
#define XRPL_RPC_MPTOKENISSUANCEID_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <memory>
#include <optional>

namespace ripple {

namespace RPC {

/**
   Add a `mpt_issuance_id` field to the `meta` input/output parameter.
   The field is only added to successful MPTokenIssuanceCreate transactions.
   The mpt_issuance_id is parsed from the sequence and the issuer in the
   MPTokenIssuance object.

   @{
 */
bool
canHaveMPTokenIssuanceID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta);

std::optional<uint192>
getIDFromCreatedIssuance(TxMeta const& transactionMeta);

void
insertMPTokenIssuanceID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta);
/** @} */

}  // namespace RPC
}  // namespace ripple

#endif
