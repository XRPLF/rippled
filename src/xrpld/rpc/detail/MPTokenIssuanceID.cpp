/** @file
 *  Implements the three-function enrichment pipeline that injects
 *  `mpt_issuance_id` into RPC JSON responses for successful
 *  `MPTokenIssuanceCreate` transactions.
 *
 *  The pipeline mirrors the structure of `DeliveredAmount.cpp`:
 *  `canHaveMPTokenIssuanceID` gates eligibility, `getIDFromCreatedIssuance`
 *  extracts the identifier from transaction metadata, and
 *  `insertMPTokenIssuanceID` composes both and writes the response field.
 *
 *  @note `getIDFromCreatedIssuance` performs a dual check on each metadata
 *      node: `sfLedgerEntryType == ltMPTOKEN_ISSUANCE` confirms the object
 *      type, and `getFName() == sfCreatedNode` confirms the operation.  Both
 *      checks are required because modified (`sfModifiedNode`) or deleted
 *      (`sfDeletedNode`) entries of the same type must be skipped — only a
 *      `CreatedNode` carries `sfNewFields` with the authoritative `sfSequence`
 *      and `sfIssuer` values needed to reconstruct the `MPTID`.
 */
#include <xrpld/rpc/MPTokenIssuanceID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl::RPC {

bool
canHaveMPTokenIssuanceID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if (tt != ttMPTOKEN_ISSUANCE_CREATE)
        return false;

    if (!isTesSuccess(transactionMeta.getResultTER()))
        return false;

    return true;
}

std::optional<MPTID>
getIDFromCreatedIssuance(TxMeta const& transactionMeta)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltMPTOKEN_ISSUANCE ||
            node.getFName() != sfCreatedNode)
            continue;

        auto const& mptNode = node.peekAtField(sfNewFields).downcast<STObject>();
        return makeMptID(mptNode.getFieldU32(sfSequence), mptNode.getAccountID(sfIssuer));
    }

    return std::nullopt;
}

void
insertMPTokenIssuanceID(
    json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveMPTokenIssuanceID(transaction, transactionMeta))
        return;

    std::optional<MPTID> result = getIDFromCreatedIssuance(transactionMeta);
    if (result)
        response[jss::mpt_issuance_id] = to_string(result.value());
}

}  // namespace xrpl::RPC
