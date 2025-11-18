#include <xrpl/json/json_value.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/NFTokenID.h>
#include <xrpl/protocol/NFTokenOfferID.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace ripple {
namespace RPC {

void
insertNFTSyntheticInJson(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertNFTokenID(response[jss::meta], transaction, transactionMeta);
    insertNFTokenOfferID(response[jss::meta], transaction, transactionMeta);
}

}  // namespace RPC
}  // namespace ripple
