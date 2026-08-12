#include <xrpld/rpc/detail/SyntheticFields.h>

#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>

#include <memory>

namespace xrpl::rpc {

void
insertAllSyntheticInJson(
    json::Value& metadata,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(metadata, ledger, transaction, transactionMeta);
    insertNFTSyntheticInJson(metadata, transaction, transactionMeta);
    insertMPTokenIssuanceID(metadata, transaction, transactionMeta);
}

void
insertAllSyntheticInJson(
    json::Value& metadata,
    JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(metadata, context, transaction, transactionMeta);
    insertNFTSyntheticInJson(metadata, transaction, transactionMeta);
    insertMPTokenIssuanceID(metadata, transaction, transactionMeta);
}

}  // namespace xrpl::rpc
