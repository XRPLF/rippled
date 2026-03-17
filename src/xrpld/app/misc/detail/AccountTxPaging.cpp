#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>

#include <xrpl/core/NetworkIDService.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

void
convertBlobsToTxResult(
    RelationalDatabase::AccountTxs& to,
    std::uint32_t ledgerIndex,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app)
{
    SerialIter it(makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const>(it);
    std::string reason;

    auto tr = std::make_shared<Transaction>(txn, reason, app);

    auto metaset = std::make_shared<TxMeta>(tr->getID(), ledgerIndex, rawMeta);

    // if properly formed meta is available we can use it to generate ctid
    if (metaset->getAsObject().isFieldPresent(sfTransactionIndex))
        tr->setStatus(
            Transaction::sqlTransactionStatus(status),
            ledgerIndex,
            metaset->getAsObject().getFieldU32(sfTransactionIndex),
            app.getNetworkIDService().getNetworkID());
    else
        tr->setStatus(Transaction::sqlTransactionStatus(status), ledgerIndex);

    to.emplace_back(std::move(tr), metaset);
};

void
saveLedgerAsync(Application& app, std::uint32_t seq)
{
    if (auto l = app.getLedgerMaster().getLedgerBySeq(seq))
        pendSaveValidated(app, l, false, false);
}

}  // namespace xrpl
