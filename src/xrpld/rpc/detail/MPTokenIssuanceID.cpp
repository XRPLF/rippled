#include <xrpld/rpc/MPTokenIssuanceID.h>

namespace ripple {

namespace RPC {

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

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

std::optional<uint192>
getIDFromCreatedIssuance(TxMeta const& transactionMeta)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltMPTOKEN_ISSUANCE ||
            node.getFName() != sfCreatedNode)
            continue;

        auto const& mptNode =
            node.peekAtField(sfNewFields).downcast<STObject>();
        return makeMptID(
            mptNode.getFieldU32(sfSequence), mptNode.getAccountID(sfIssuer));
    }

    return std::nullopt;
}

void
insertMPTokenIssuanceID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveMPTokenIssuanceID(transaction, transactionMeta))
        return;

    std::optional<uint192> result = getIDFromCreatedIssuance(transactionMeta);
    if (result)
        response[jss::mpt_issuance_id] = to_string(result.value());
}

}  // namespace RPC
}  // namespace ripple
