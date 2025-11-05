#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/NFTokenOfferID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace ripple {

bool
canHaveNFTokenOfferID(
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    TxType const tt = serializedTx->getTxnType();
    if (!(tt == ttNFTOKEN_MINT && serializedTx->isFieldPresent(sfAmount)) &&
        tt != ttNFTOKEN_CREATE_OFFER)
        return false;

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

std::optional<uint256>
getOfferIDFromCreatedOffer(TxMeta const& transactionMeta)
{
    for (STObject const& node : transactionMeta.getNodes())
    {
        if (node.getFieldU16(sfLedgerEntryType) != ltNFTOKEN_OFFER ||
            node.getFName() != sfCreatedNode)
            continue;

        return node.getFieldH256(sfLedgerIndex);
    }
    return std::nullopt;
}

void
insertNFTokenOfferID(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (!canHaveNFTokenOfferID(transaction, transactionMeta))
        return;

    std::optional<uint256> result = getOfferIDFromCreatedOffer(transactionMeta);

    if (result.has_value())
        response[jss::offer_id] = to_string(result.value());
}

}  // namespace ripple
