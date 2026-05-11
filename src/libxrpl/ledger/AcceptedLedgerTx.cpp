#include <xrpl/ledger/AcceptedLedgerTx.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl {

AcceptedLedgerTx::AcceptedLedgerTx(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& met)
    : txn_(txn)
    , meta_(txn->getTransactionID(), ledger->seq(), *met)
    , affected_(meta_.getAffectedAccounts())
{
    XRPL_ASSERT(!ledger->open(), "xrpl::AcceptedLedgerTx::AcceptedLedgerTx : valid ledger state");

    Serializer s;
    met->add(s);
    rawMeta_ = std::move(s.modData());

    json_ = json::ValueType::Object;
    json_[jss::transaction] = txn_->getJson(JsonOptions::Values::None);

    json_[jss::meta] = meta_.getJson(JsonOptions::Values::None);
    json_[jss::raw_meta] = strHex(rawMeta_);

    json_[jss::result] = transHuman(meta_.getResultTER());

    if (!affected_.empty())
    {
        json::Value& affected = (json_[jss::affected] = json::ValueType::Array);
        for (auto const& account : affected_)
            affected.append(toBase58(account));
    }

    if (txn_->getTxnType() == ttOFFER_CREATE)
    {
        auto const& account = txn_->getAccountID(sfAccount);
        auto const amount = txn_->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.getIssuer())
        {
            auto const ownerFunds = accountFunds(
                *ledger,
                account,
                amount,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                beast::Journal{beast::Journal::getNullSink()});
            json_[jss::transaction][jss::owner_funds] = ownerFunds.getText();
        }
    }
}

std::string
AcceptedLedgerTx::getEscMeta() const
{
    XRPL_ASSERT(!rawMeta_.empty(), "xrpl::AcceptedLedgerTx::getEscMeta : metadata is set");
    return sqlBlobLiteral(rawMeta_);
}

}  // namespace xrpl
